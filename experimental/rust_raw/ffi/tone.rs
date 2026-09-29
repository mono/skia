// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Test-only natural cubic interpolation of validated SDR ProfileToneCurve
//! samples. It is not an Adobe default tone curve or an enabled renderer.

use super::dng::{valid_sdr_tone_curve, ByteOrder, Error, Tag};

#[path = "acr3_default.rs"]
mod acr3_default;

pub(crate) struct ToneCurve {
    points: Vec<(f64, f64)>,
    second: Vec<f64>,
}

impl ToneCurve {
    pub(crate) fn parse(data: &[u8], order: ByteOrder) -> Result<Self, Error> {
        if data.len() % 8 != 0 {
            return Err(Error::Invalid);
        }
        let count = u32::try_from(data.len() / 4).map_err(|_| Error::Invalid)?;
        let tag = Tag {
            id: 50940,
            kind: 11,
            count,
            value: data,
        };
        if !valid_sdr_tone_curve(&tag, order) {
            return Err(Error::Invalid);
        }
        let length = data.len() / 8;
        let mut points = Vec::new();
        points
            .try_reserve_exact(length)
            .map_err(|_| Error::OutOfMemory)?;
        for pair in data.chunks_exact(8) {
            points.push((
                f64::from(f32::from_bits(order.u32(&pair[..4]))),
                f64::from(f32::from_bits(order.u32(&pair[4..]))),
            ));
        }
        let mut second = Vec::new();
        let mut diagonal = Vec::new();
        let mut right = Vec::new();
        for scratch in [&mut second, &mut diagonal, &mut right] {
            scratch
                .try_reserve_exact(length)
                .map_err(|_| Error::OutOfMemory)?;
            scratch.resize(length, 0.0);
        }
        for index in 1..length - 1 {
            let previous = points[index].0 - points[index - 1].0;
            let next = points[index + 1].0 - points[index].0;
            let left_slope = (points[index].1 - points[index - 1].1) / previous;
            let right_slope = (points[index + 1].1 - points[index].1) / next;
            diagonal[index] = 2.0 * (previous + next);
            right[index] = 6.0 * (right_slope - left_slope);
            if index > 1 {
                let factor = previous / diagonal[index - 1];
                diagonal[index] -= factor * previous;
                right[index] -= factor * right[index - 1];
            }
            if !diagonal[index].is_finite() || diagonal[index] <= 0.0 || !right[index].is_finite() {
                return Err(Error::Invalid);
            }
        }
        for index in (1..length - 1).rev() {
            let next = points[index + 1].0 - points[index].0;
            second[index] = (right[index] - next * second[index + 1]) / diagonal[index];
            if !second[index].is_finite() {
                return Err(Error::Invalid);
            }
        }
        Ok(Self { points, second })
    }

    pub(crate) fn evaluate(&self, value: f64) -> Result<f64, Error> {
        if !value.is_finite() || !(0.0..=1.0).contains(&value) {
            return Err(Error::Invalid);
        }
        let upper = self.points.partition_point(|&(x, _)| x < value);
        let index = upper.saturating_sub(1).min(self.points.len() - 2);
        let (x0, y0) = self.points[index];
        let (x1, y1) = self.points[index + 1];
        let span = x1 - x0;
        let left = (x1 - value) / span;
        let right = (value - x0) / span;
        let result = left * y0
            + right * y1
            + ((left * left * left - left) * self.second[index]
                + (right * right * right - right) * self.second[index + 1])
                * span
                * span
                / 6.0;
        if !result.is_finite() {
            return Err(Error::Invalid);
        }
        Ok(result)
    }
}

// Derived from Adobe DNG SDK 1.7.1.2724,
// source/dng_1d_table.h/cpp::dng_1d_table::Initialize/Interpolate.
// Copyright 2006-2019 Adobe Systems Incorporated. All Rights Reserved.
// See experimental/rust_raw/licenses/LICENSE.adobe-dng-sdk and PROVENANCE.md.
pub(crate) struct SdkToneTable {
    samples: Vec<f32>,
}

impl SdkToneTable {
    const SIZE: usize = 4096;

    pub(crate) fn from_curve(curve: &ToneCurve) -> Result<Self, Error> {
        Self::from_function(|value| curve.evaluate(value))
    }

    pub(crate) fn from_function(
        mut evaluate: impl FnMut(f64) -> Result<f64, Error>,
    ) -> Result<Self, Error> {
        let mut samples = Vec::new();
        samples
            .try_reserve_exact(Self::SIZE + 2)
            .map_err(|_| Error::OutOfMemory)?;
        for index in 0..=Self::SIZE {
            let value = evaluate(index as f64 / Self::SIZE as f64)? as f32;
            if !value.is_finite() {
                return Err(Error::Invalid);
            }
            samples.push(value);
        }
        samples.push(samples[Self::SIZE]);
        Ok(Self { samples })
    }

    pub(crate) fn interpolate(&self, value: f32) -> Result<f32, Error> {
        if !value.is_finite() || !(0.0..=1.0).contains(&value) {
            return Err(Error::Invalid);
        }
        let position = value * Self::SIZE as f32;
        let index = position as usize;
        let fraction = position - index as f32;
        Ok(self.samples[index] * (1.0 - fraction) + self.samples[index + 1] * fraction)
    }
}

// Derived from Adobe DNG SDK 1.7.1.2724,
// source/dng_render.cpp::dng_tone_curve_acr3_default::Evaluate.
// Copyright 2006-2023 Adobe Systems Incorporated. All Rights Reserved.
// See experimental/rust_raw/licenses/LICENSE.adobe-dng-sdk and PROVENANCE.md.
pub(crate) fn sdk_acr3_default_tone(value: f64) -> Result<f64, Error> {
    if !value.is_finite() || !(0.0..=1.0).contains(&value) {
        return Err(Error::Invalid);
    }
    let entries = &acr3_default::SDK_ACR3_DEFAULT;
    let position = value as f32 * (entries.len() - 1) as f32;
    let index = (position as usize).min(entries.len() - 2);
    let fraction = position - index as f32;
    let result = entries[index] * (1.0 - fraction) + entries[index + 1] * fraction;
    if !result.is_finite() {
        return Err(Error::Invalid);
    }
    Ok(f64::from(result))
}

// Derived from Adobe DNG SDK 1.7.1.2724,
// source/dng_render.cpp::dng_render_task::Start/
// dng_function_exposure_ramp::Evaluate/DoBaseline1DFunction.
// Copyright 2006-2023 Adobe Systems Incorporated. All Rights Reserved.
// See experimental/rust_raw/licenses/LICENSE.adobe-dng-sdk and PROVENANCE.md.
// Only the SDR, zero-exposure case is modeled; HDR and exposure compensation
// require separate, verified processing.
pub(crate) struct SdkExposureRamp {
    black: f64,
    slope: f64,
    radius: f64,
    quadratic_scale: f64,
}

impl SdkExposureRamp {
    pub(crate) fn for_zero_exposure(
        shadows: f64,
        shadow_scale: f64,
        stage3_gain: f64,
    ) -> Result<Self, Error> {
        if [shadows, shadow_scale, stage3_gain]
            .iter()
            .any(|value| !value.is_finite())
            || shadows < 0.0
            || shadow_scale <= 0.0
            || stage3_gain <= 0.0
        {
            return Err(Error::Invalid);
        }
        let black = shadows * shadow_scale * stage3_gain * 0.001;
        if !black.is_finite() {
            return Err(Error::Invalid);
        }
        let black = black.min(0.99);
        let slope = 1.0 / (1.0 - black);
        let radius = (0.5 * black).min((1.0 / 16.0) / slope);
        let quadratic_scale = if radius > 0.0 {
            slope / (4.0 * radius)
        } else {
            0.0
        };
        if [slope, radius, quadratic_scale]
            .iter()
            .any(|value| !value.is_finite())
        {
            return Err(Error::Invalid);
        }
        Ok(Self {
            black,
            slope,
            radius,
            quadratic_scale,
        })
    }

    pub(crate) fn evaluate(&self, input: f32) -> Result<f32, Error> {
        if !input.is_finite() {
            return Err(Error::Invalid);
        }
        let x = f64::from(input.clamp(0.0, 1.0));
        let value = if x <= self.black - self.radius {
            0.0
        } else if x >= self.black + self.radius {
            (x - self.black) * self.slope
        } else {
            let distance = x - (self.black - self.radius);
            self.quadratic_scale * distance * distance
        };
        if !value.is_finite() {
            return Err(Error::Invalid);
        }
        Ok((value as f32).clamp(0.0, 1.0))
    }
}

// Derived from Adobe DNG SDK 1.7.1.2724,
// source/dng_reference.cpp::RefBaselineRGBTone.
// Copyright 2006-2023 Adobe Systems Incorporated. All Rights Reserved.
// See experimental/rust_raw/licenses/LICENSE.adobe-dng-sdk and PROVENANCE.md.
// This applies in intermediate RGB, before the final output color transform.
pub(crate) fn apply_sdk_rgb_tone(
    rgb: [f32; 3],
    mut curve: impl FnMut(f32) -> Result<f32, Error>,
) -> Result<[f32; 3], Error> {
    if rgb.iter().any(|value| !value.is_finite()) {
        return Err(Error::Invalid);
    }
    let clipped = rgb.map(|value| value.clamp(0.0, 1.0));
    let mut order = [0, 1, 2];
    order.sort_by(|&a, &b| clipped[a].total_cmp(&clipped[b]));
    let [low, middle, high] = order;
    if clipped[low] == clipped[high] {
        let value = curve(clipped[low])?;
        return if value.is_finite() {
            Ok([value; 3])
        } else {
            Err(Error::Invalid)
        };
    }
    let top = curve(clipped[high])?;
    let bottom = curve(clipped[low])?;
    let middle_value = bottom
        + ((top - bottom) * (clipped[middle] - clipped[low]) / (clipped[high] - clipped[low]));
    if [top, bottom, middle_value]
        .iter()
        .any(|value| !value.is_finite())
    {
        return Err(Error::Invalid);
    }
    let mut output = [0.0; 3];
    output[low] = bottom;
    output[middle] = middle_value;
    output[high] = top;
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::{
        acr3_default, apply_sdk_rgb_tone, sdk_acr3_default_tone, ByteOrder, Error, SdkExposureRamp,
        SdkToneTable, ToneCurve,
    };

    fn points(values: &[(f32, f32)], order: ByteOrder) -> Vec<u8> {
        let mut bytes = Vec::new();
        for &(x, y) in values {
            for value in [x, y] {
                bytes.extend_from_slice(&match order {
                    ByteOrder::Little => value.to_le_bytes(),
                    ByteOrder::Big => value.to_be_bytes(),
                });
            }
        }
        bytes
    }

    #[test]
    fn natural_cubic_matches_identity_and_independent_three_point_values() {
        for order in [ByteOrder::Little, ByteOrder::Big] {
            let identity = ToneCurve::parse(&points(&[(0.0, 0.0), (1.0, 1.0)], order), order)
                .expect("identity curve");
            for sample in 0..=255 {
                let x = f64::from(sample) / 255.0;
                assert!((identity.evaluate(x).unwrap() - x).abs() < 1e-12);
            }
            let curve =
                ToneCurve::parse(&points(&[(0.0, 0.0), (0.5, 0.7), (1.0, 1.0)], order), order)
                    .expect("three point tone curve");
            assert!((curve.evaluate(0.25).unwrap() - 0.3875).abs() < 1e-6);
            assert!((curve.evaluate(0.75).unwrap() - 0.8875).abs() < 1e-6);
            assert_eq!(curve.evaluate(0.0), Ok(0.0));
            assert_eq!(curve.evaluate(1.0), Ok(1.0));
        }
    }

    #[test]
    fn malformed_curve_and_invalid_inputs_fail_explicitly() {
        let order = ByteOrder::Little;
        for invalid in [
            points(&[(0.0, 0.0)], order),
            points(&[(0.0, 0.0), (0.5, 0.7), (0.5, 1.0)], order),
            points(&[(0.0, 0.0), (f32::NAN, 0.7), (1.0, 1.0)], order),
            points(&[(0.0, 0.0), (0.5, 0.7), (1.0, 1.2)], order),
        ] {
            assert!(matches!(
                ToneCurve::parse(&invalid, order),
                Err(Error::Invalid)
            ));
        }
        let valid = ToneCurve::parse(&points(&[(0.0, 0.0), (1.0, 1.0)], order), order).unwrap();
        assert_eq!(valid.evaluate(f64::NAN), Err(Error::Invalid));
        assert_eq!(valid.evaluate(-0.01), Err(Error::Invalid));
        assert_eq!(valid.evaluate(1.01), Err(Error::Invalid));
    }

    #[test]
    fn sdk_intermediate_rgb_tone_preserves_color_order() {
        let curve = ToneCurve::parse(
            &points(&[(0.0, 0.0), (0.5, 0.7), (1.0, 1.0)], ByteOrder::Little),
            ByteOrder::Little,
        )
        .expect("valid three-point curve");
        let tone = |value: f32| curve.evaluate(f64::from(value)).map(|v| v as f32);
        assert_eq!(
            apply_sdk_rgb_tone([1.0, 0.5, 0.0], tone),
            Ok([1.0, 0.5, 0.0])
        );
        assert_eq!(
            apply_sdk_rgb_tone([0.0, 1.0, 0.5], tone),
            Ok([0.0, 1.0, 0.5])
        );
        assert_eq!(apply_sdk_rgb_tone([0.5; 3], tone), Ok([0.7; 3]));
        assert_eq!(
            apply_sdk_rgb_tone([1.0, 1.0, 0.0], tone),
            Ok([1.0, 1.0, 0.0])
        );
        assert_eq!(
            apply_sdk_rgb_tone([1.0, 0.0, 0.0], tone),
            Ok([1.0, 0.0, 0.0])
        );
        assert_eq!(
            apply_sdk_rgb_tone([-1.0, 0.5, 2.0], tone),
            Ok([0.0, 0.5, 1.0])
        );
        let color = apply_sdk_rgb_tone([0.2, 0.9, 0.6], tone).expect("colored output");
        let low = curve.evaluate(0.2).expect("low") as f32;
        let high = curve.evaluate(0.9).expect("high") as f32;
        let expected = low + (high - low) * ((0.6 - 0.2) / (0.9 - 0.2));
        assert!((color[0] - low).abs() < 1e-6);
        assert!((color[1] - high).abs() < 1e-6);
        assert!((color[2] - expected).abs() < 1e-6);
        assert_ne!(color[2], tone(0.6).expect("naive per-channel tone"));
        assert_eq!(
            apply_sdk_rgb_tone([f32::NAN, 0.5, 1.0], tone),
            Err(Error::Invalid)
        );
        assert_eq!(
            apply_sdk_rgb_tone([0.0, 0.5, 1.0], |_| Err(Error::Unsupported)),
            Err(Error::Unsupported)
        );
        assert_eq!(
            apply_sdk_rgb_tone([0.0, 0.5, 1.0], |_| Ok(f32::INFINITY)),
            Err(Error::Invalid)
        );
    }

    #[test]
    fn sdk_tone_table_interpolates_checked_curve_samples() {
        let curve = ToneCurve::parse(
            &points(&[(0.0, 0.0), (0.5, 0.7), (1.0, 1.0)], ByteOrder::Little),
            ByteOrder::Little,
        )
        .expect("valid three-point curve");
        let table = SdkToneTable::from_curve(&curve).expect("bounded SDK-sized table");
        assert_eq!(table.samples.len(), 4098);
        assert_eq!(table.interpolate(0.0), Ok(0.0));
        assert_eq!(table.interpolate(0.5), Ok(0.7));
        assert_eq!(table.interpolate(1.0), Ok(1.0));
        for sample in 0..=255 {
            let x = sample as f32 / 255.0;
            let expected = curve.evaluate(f64::from(x)).expect("curve sample") as f32;
            assert!(
                (table.interpolate(x).expect("table sample") - expected).abs() < 1e-6,
                "curve at {sample}"
            );
        }
        assert_eq!(
            apply_sdk_rgb_tone([1.0, 0.5, 0.0], |x| table.interpolate(x)),
            Ok([1.0, 0.5, 0.0])
        );
        assert_eq!(table.interpolate(f32::NAN), Err(Error::Invalid));
        assert_eq!(table.interpolate(-1.0), Err(Error::Invalid));
        assert_eq!(table.interpolate(1.01), Err(Error::Invalid));
    }

    #[test]
    fn pinned_sdk_acr3_default_tone_is_bounded_and_table_driven() {
        assert_eq!(acr3_default::SDK_ACR3_DEFAULT.len(), 1025);
        assert_eq!(sdk_acr3_default_tone(0.0), Ok(0.0));
        assert_eq!(sdk_acr3_default_tone(1.0), Ok(1.0));
        for index in [1, 16, 256, 512, 768, 1023] {
            assert_eq!(
                sdk_acr3_default_tone(index as f64 / 1024.0),
                Ok(f64::from(acr3_default::SDK_ACR3_DEFAULT[index]))
            );
        }
        let table = SdkToneTable::from_function(sdk_acr3_default_tone)
            .expect("sampled SDK ACR3 default tone");
        assert_eq!(table.interpolate(0.0), Ok(0.0));
        assert_eq!(table.interpolate(1.0), Ok(1.0));
        assert_eq!(sdk_acr3_default_tone(f64::NAN), Err(Error::Invalid));
        assert_eq!(sdk_acr3_default_tone(-0.01), Err(Error::Invalid));
        assert_eq!(sdk_acr3_default_tone(1.01), Err(Error::Invalid));
    }

    #[test]
    fn sdk_sdr_auto_black_ramp_has_checked_shadow_transition() {
        let identity =
            SdkExposureRamp::for_zero_exposure(0.0, 1.0, 1.0).expect("BlackRender=None ramp");
        for value in [0.0, 0.003, 0.1, 0.75, 1.0] {
            assert_eq!(identity.evaluate(value), Ok(value));
        }
        // dng_render::dng_render defaults to fShadows = 5 for scene data.
        let auto =
            SdkExposureRamp::for_zero_exposure(5.0, 1.0, 1.0).expect("default Auto-black ramp");
        assert_eq!(auto.evaluate(0.0), Ok(0.0));
        assert_eq!(auto.evaluate(0.0025), Ok(0.0));
        assert_eq!(auto.evaluate(1.0), Ok(1.0));
        let midpoint = 0.005_f32;
        let expected = (1.0 / 0.995 / (4.0 * 0.0025) * 0.0025 * 0.0025) as f32;
        assert!((auto.evaluate(midpoint).unwrap() - expected).abs() < 1e-7);
        assert_eq!(auto.evaluate(f32::NAN), Err(Error::Invalid));
        assert!(matches!(
            SdkExposureRamp::for_zero_exposure(5.0, -1.0, 1.0),
            Err(Error::Invalid)
        ));
        assert!(matches!(
            SdkExposureRamp::for_zero_exposure(5.0, 1.0, f64::INFINITY),
            Err(Error::Invalid)
        ));
        assert!(matches!(
            SdkExposureRamp::for_zero_exposure(f64::MAX, f64::MAX, 1.0),
            Err(Error::Invalid)
        ));
    }
}
