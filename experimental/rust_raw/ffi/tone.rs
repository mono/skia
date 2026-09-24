// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Test-only natural cubic interpolation of validated SDR ProfileToneCurve
//! samples. It is not an Adobe default tone curve or an enabled renderer.

use super::dng::{valid_sdr_tone_curve, ByteOrder, Error, Tag};

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

#[cfg(test)]
mod tests {
    use super::{ByteOrder, Error, ToneCurve};

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
}
