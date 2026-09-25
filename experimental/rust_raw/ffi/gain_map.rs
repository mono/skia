// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Checked DNG profile gain tables. Only proven identity tables are allowed
//! in the currently supported final RGB8 profile; other gain stays gated.

use super::dng::{range, ByteOrder, Error};

#[derive(Clone, Copy)]
pub(crate) enum GainTag {
    ProfileGainTableMap,
    ProfileGainTableMap2,
}

pub(crate) struct GainTable<'a> {
    data: &'a [u8],
    order: ByteOrder,
    points_v: usize,
    points_h: usize,
    points_n: usize,
    spacing_v: f64,
    spacing_h: f64,
    origin_v: f64,
    origin_h: f64,
    weights: [f32; 5],
    data_type: u32,
    gamma: f32,
    gain_min: f32,
    gain_max: f32,
    table_offset: usize,
    sample_bytes: usize,
    identity: bool,
}

fn float64(data: &[u8], offset: usize, order: ByteOrder) -> Result<f64, Error> {
    let bytes: [u8; 8] = range(data, offset, 8)?
        .try_into()
        .map_err(|_| Error::Invalid)?;
    let bits = match order {
        ByteOrder::Little => u64::from_le_bytes(bytes),
        ByteOrder::Big => u64::from_be_bytes(bytes),
    };
    Ok(f64::from_bits(bits))
}

fn float32(data: &[u8], offset: usize, order: ByteOrder) -> Result<f32, Error> {
    Ok(f32::from_bits(order.u32(range(data, offset, 4)?)))
}

fn float16(data: &[u8], offset: usize, order: ByteOrder) -> Result<f32, Error> {
    let bits = order.u16(range(data, offset, 2)?);
    let exponent = (bits >> 10) & 0x1f;
    let fraction = bits & 0x3ff;
    let magnitude = if exponent == 0 {
        f32::from(fraction) * 2.0_f32.powi(-24)
    } else if exponent == 0x1f {
        return Err(Error::Invalid);
    } else {
        (1.0 + f32::from(fraction) / 1024.0) * 2.0_f32.powi(i32::from(exponent) - 15)
    };
    Ok(if bits & 0x8000 != 0 {
        -magnitude
    } else {
        magnitude
    })
}

impl<'a> GainTable<'a> {
    pub(crate) fn parse(data: &'a [u8], order: ByteOrder, tag: GainTag) -> Result<Self, Error> {
        let (table_offset, data_type, gamma, gain_min, gain_max): (usize, u32, f32, f32, f32) =
            match tag {
                GainTag::ProfileGainTableMap => (64, 3, 1.0, 0.0, 0.0),
                GainTag::ProfileGainTableMap2 => {
                    let kind = order.u32(range(data, 64, 4)?);
                    let gamma = float32(data, 68, order)?;
                    let min = float32(data, 72, order)?;
                    let max = float32(data, 76, order)?;
                    (80, kind, gamma, min, max)
                }
            };
        let sample_bytes: usize = match data_type {
            0 => 1,
            1 | 2 => 2,
            3 => 4,
            _ => return Err(Error::Unsupported),
        };
        let points_v = order.u32(range(data, 0, 4)?) as usize;
        let points_h = order.u32(range(data, 4, 4)?) as usize;
        let points_n = order.u32(range(data, 40, 4)?) as usize;
        if points_v == 0 || points_h == 0 || points_n == 0 {
            return Err(Error::Invalid);
        }
        let samples = points_v
            .checked_mul(points_h)
            .and_then(|value| value.checked_mul(points_n))
            .ok_or(Error::Invalid)?;
        let bytes = samples.checked_mul(sample_bytes).ok_or(Error::Invalid)?;
        if data.len() != table_offset.checked_add(bytes).ok_or(Error::Invalid)? {
            return Err(Error::Invalid);
        }
        let spacing_v = float64(data, 8, order)?;
        let spacing_h = float64(data, 16, order)?;
        let origin_v = float64(data, 24, order)?;
        let origin_h = float64(data, 32, order)?;
        if !spacing_v.is_finite()
            || spacing_v <= 0.0
            || !spacing_h.is_finite()
            || spacing_h <= 0.0
            || !origin_v.is_finite()
            || !origin_h.is_finite()
            || !gamma.is_finite()
            || !(0.125..=8.0).contains(&gamma)
        {
            return Err(Error::Invalid);
        }
        const MIN_GAIN: f32 = 1.0 / 4096.0;
        const MAX_GAIN: f32 = 4096.0;
        // The configured SDK also validates the ignored float-storage bounds.
        if matches!(tag, GainTag::ProfileGainTableMap2)
            && (!gain_min.is_finite()
                || gain_min < MIN_GAIN
                || !gain_max.is_finite()
                || gain_max > MAX_GAIN
                || (data_type <= 1 && (gain_min > MAX_GAIN || gain_max < MIN_GAIN)))
        {
            return Err(Error::Invalid);
        }
        let mut weights = [0.0; 5];
        for (index, weight) in weights.iter_mut().enumerate() {
            *weight = float32(data, 44 + index * 4, order)?;
            if !weight.is_finite() {
                return Err(Error::Invalid);
            }
        }
        let mut result = Self {
            data,
            order,
            points_v,
            points_h,
            points_n,
            spacing_v,
            spacing_h,
            origin_v,
            origin_h,
            weights,
            data_type,
            gamma,
            gain_min,
            gain_max,
            table_offset,
            sample_bytes,
            identity: true,
        };
        for index in 0..samples {
            let value = result.sample(index)?;
            if !value.is_finite()
                || (result.data_type >= 2 && !(MIN_GAIN..=MAX_GAIN).contains(&value))
            {
                return Err(Error::Invalid);
            }
            let gain = result.gain_from_stored(f64::from(value));
            if !gain.is_finite() || gain < 0.0 {
                return Err(Error::Invalid);
            }
            result.identity &= gain == 1.0;
        }
        Ok(result)
    }

    pub(crate) fn is_identity(&self) -> bool {
        self.identity
    }

    fn gain_from_stored(&self, value: f64) -> f64 {
        if self.data_type <= 1 {
            let max_integer = if self.data_type == 0 { 255.0 } else { 65535.0 };
            f64::from(self.gain_min)
                + (value / max_integer) * (f64::from(self.gain_max) - f64::from(self.gain_min))
        } else {
            value
        }
    }

    fn sample(&self, index: usize) -> Result<f32, Error> {
        let offset = index
            .checked_mul(self.sample_bytes)
            .and_then(|value| self.table_offset.checked_add(value))
            .ok_or(Error::Invalid)?;
        match self.data_type {
            0 => Ok(f32::from(range(self.data, offset, 1)?[0])),
            1 => Ok(f32::from(self.order.u16(range(self.data, offset, 2)?))),
            2 => float16(self.data, offset, self.order),
            3 => float32(self.data, offset, self.order),
            _ => Err(Error::Unsupported),
        }
    }

    fn point(&self, v: usize, h: usize, n: usize) -> Result<f64, Error> {
        let index = v
            .checked_mul(self.points_h)
            .and_then(|value| value.checked_add(h))
            .and_then(|value| value.checked_mul(self.points_n))
            .and_then(|value| value.checked_add(n))
            .ok_or(Error::Invalid)?;
        Ok(f64::from(self.sample(index)?))
    }

    fn axis(
        pixel: u32,
        extent: u32,
        points: usize,
        origin: f64,
        spacing: f64,
    ) -> Result<(usize, usize, f64), Error> {
        if extent == 0 || pixel >= extent {
            return Err(Error::Invalid);
        }
        let coordinate = ((f64::from(pixel) + 0.5) / f64::from(extent) - origin) / spacing;
        if !coordinate.is_finite() {
            return Err(Error::Invalid);
        }
        let position = coordinate.clamp(0.0, (points - 1) as f64);
        let low = position.floor() as usize;
        let high = (low + 1).min(points - 1);
        Ok((low, high, position - low as f64))
    }

    pub(crate) fn gain_at(
        &self,
        x: u32,
        y: u32,
        width: u32,
        height: u32,
        rimm_rgb: [f32; 3],
    ) -> Result<f32, Error> {
        if rimm_rgb.iter().any(|value| !value.is_finite()) {
            return Err(Error::Invalid);
        }
        let (v0, v1, tv) = Self::axis(y, height, self.points_v, self.origin_v, self.spacing_v)?;
        let (h0, h1, th) = Self::axis(x, width, self.points_h, self.origin_h, self.spacing_h)?;
        let [r, g, b] = rimm_rgb.map(f64::from);
        let components = [r, g, b, r.min(g).min(b), r.max(g).max(b)];
        let input: f64 = components
            .iter()
            .zip(self.weights)
            .map(|(sample, weight)| sample * f64::from(weight))
            .sum();
        if !input.is_finite() {
            return Err(Error::Invalid);
        }
        let depth = input.clamp(0.0, 1.0).powf(f64::from(self.gamma)) * self.points_n as f64;
        if !depth.is_finite() {
            return Err(Error::Invalid);
        }
        let depth = depth.min((self.points_n - 1) as f64);
        let n0 = depth.floor() as usize;
        let n1 = (n0 + 1).min(self.points_n - 1);
        let tn = depth - n0 as f64;
        let table = |n| -> Result<f64, Error> {
            let row0 = self.point(v0, h0, n)? * (1.0 - th) + self.point(v0, h1, n)? * th;
            let row1 = self.point(v1, h0, n)? * (1.0 - th) + self.point(v1, h1, n)? * th;
            Ok(row0 * (1.0 - tv) + row1 * tv)
        };
        let value = table(n0)? * (1.0 - tn) + table(n1)? * tn;
        let gain = self.gain_from_stored(value);
        if !gain.is_finite() || gain < 0.0 {
            return Err(Error::Invalid);
        }
        Ok(gain as f32)
    }
}

#[cfg(test)]
mod tests {
    use super::{ByteOrder, Error, GainTable, GainTag};

    fn fixture(order: ByteOrder, tag: GainTag, data_type: u32) -> Vec<u8> {
        let mut bytes = Vec::new();
        let u32_bytes = |value: u32| match order {
            ByteOrder::Little => value.to_le_bytes(),
            ByteOrder::Big => value.to_be_bytes(),
        };
        let u64_bytes = |value: u64| match order {
            ByteOrder::Little => value.to_le_bytes(),
            ByteOrder::Big => value.to_be_bytes(),
        };
        for value in [3, 3] {
            bytes.extend_from_slice(&u32_bytes(value));
        }
        for value in [0.5f64, 0.5, 0.0, 0.0] {
            bytes.extend_from_slice(&u64_bytes(value.to_bits()));
        }
        bytes.extend_from_slice(&u32_bytes(2));
        for value in [1.0f32, 0.0, 0.0, 0.0, 0.0] {
            bytes.extend_from_slice(&u32_bytes(value.to_bits()));
        }
        if matches!(tag, GainTag::ProfileGainTableMap2) {
            bytes.extend_from_slice(&u32_bytes(data_type));
            bytes.extend_from_slice(&u32_bytes(1.0f32.to_bits()));
            bytes.extend_from_slice(&u32_bytes(1.0f32.to_bits()));
            bytes.extend_from_slice(&u32_bytes(4.0f32.to_bits()));
        }
        for v in 0..3 {
            for h in 0..3 {
                for _n in 0..2 {
                    let center = v == 1 && h == 1;
                    match data_type {
                        0 => bytes.push(if center { 255 } else { 0 }),
                        1 | 2 => {
                            let value = if data_type == 1 {
                                if center {
                                    u16::MAX
                                } else {
                                    0
                                }
                            } else if center {
                                0x4400
                            } else {
                                0x3c00
                            };
                            bytes.extend_from_slice(&match order {
                                ByteOrder::Little => value.to_le_bytes(),
                                ByteOrder::Big => value.to_be_bytes(),
                            });
                        }
                        3 => bytes.extend_from_slice(&u32_bytes(
                            (if center { 4.0f32 } else { 1.0 }).to_bits(),
                        )),
                        _ => unreachable!(),
                    }
                }
            }
        }
        bytes
    }

    #[test]
    fn public_storage_types_agree_at_center_and_between_tables() {
        for order in [ByteOrder::Little, ByteOrder::Big] {
            for (tag, kind) in [
                (GainTag::ProfileGainTableMap2, 0),
                (GainTag::ProfileGainTableMap2, 1),
                (GainTag::ProfileGainTableMap2, 2),
                (GainTag::ProfileGainTableMap2, 3),
                (GainTag::ProfileGainTableMap, 3),
            ] {
                let blob = fixture(order, tag, kind);
                let parsed = GainTable::parse(&blob, order, tag).expect("valid map");
                assert!(!parsed.is_identity());
                assert_eq!(parsed.gain_at(0, 0, 1, 1, [0.5, 0.0, 0.0]), Ok(4.0));
                assert_eq!(parsed.gain_at(0, 0, 2, 2, [0.5, 0.0, 0.0]), Ok(1.75));
                assert_eq!(parsed.gain_at(1, 1, 2, 2, [0.5, 0.0, 0.0]), Ok(1.75));
            }
        }
    }

    #[test]
    fn identity_requires_every_decoded_sample_to_map_to_one() {
        for order in [ByteOrder::Little, ByteOrder::Big] {
            for (tag, kind) in [
                (GainTag::ProfileGainTableMap2, 0),
                (GainTag::ProfileGainTableMap2, 1),
                (GainTag::ProfileGainTableMap2, 2),
                (GainTag::ProfileGainTableMap2, 3),
                (GainTag::ProfileGainTableMap, 3),
            ] {
                let mut blob = fixture(order, tag, kind);
                let (offset, sample) = match kind {
                    0 => (80, vec![0]),
                    1 => (80, vec![0, 0]),
                    2 => (
                        80,
                        match order {
                            ByteOrder::Little => 0x3c00u16.to_le_bytes(),
                            ByteOrder::Big => 0x3c00u16.to_be_bytes(),
                        }
                        .to_vec(),
                    ),
                    3 => (
                        if matches!(tag, GainTag::ProfileGainTableMap) {
                            64
                        } else {
                            80
                        },
                        match order {
                            ByteOrder::Little => 1.0f32.to_le_bytes(),
                            ByteOrder::Big => 1.0f32.to_be_bytes(),
                        }
                        .to_vec(),
                    ),
                    _ => unreachable!(),
                };
                for value in blob[offset..].chunks_exact_mut(sample.len()) {
                    value.copy_from_slice(&sample);
                }
                let identity = GainTable::parse(&blob, order, tag).expect("identity table");
                assert!(identity.is_identity());
                for (x, y, color) in [
                    (0, 0, [0.0; 3]),
                    (1, 1, [0.5, 0.25, 0.75]),
                    (255, 255, [1.0; 3]),
                ] {
                    assert_eq!(identity.gain_at(x, y, 256, 256, color), Ok(1.0));
                }
                let last = blob.len() - sample.len();
                let non_unity = match kind {
                    0 => vec![255],
                    1 => vec![255; 2],
                    2 => (match order {
                        ByteOrder::Little => 0x4400u16.to_le_bytes(),
                        ByteOrder::Big => 0x4400u16.to_be_bytes(),
                    })
                    .to_vec(),
                    3 => (match order {
                        ByteOrder::Little => 4.0f32.to_le_bytes(),
                        ByteOrder::Big => 4.0f32.to_be_bytes(),
                    })
                    .to_vec(),
                    _ => unreachable!(),
                };
                blob[last..].copy_from_slice(&non_unity);
                assert!(!GainTable::parse(&blob, order, tag)
                    .expect("non-unity table")
                    .is_identity());
            }
        }
    }

    #[test]
    fn direct_table_index_uses_points_n_and_gamma() {
        let mut blob = fixture(ByteOrder::Little, GainTag::ProfileGainTableMap2, 3);
        let first_center = 80 + ((1 * 3 + 1) * 2) * 4;
        blob[first_center..first_center + 4].copy_from_slice(&1.0f32.to_le_bytes());
        let second_center = first_center + 4;
        blob[second_center..second_center + 4].copy_from_slice(&4.0f32.to_le_bytes());
        let map = GainTable::parse(&blob, ByteOrder::Little, GainTag::ProfileGainTableMap2)
            .expect("valid map");
        assert_eq!(map.gain_at(0, 0, 1, 1, [0.25, 0.0, 0.0]), Ok(2.5));
        blob[68..72].copy_from_slice(&2.0f32.to_le_bytes());
        let map = GainTable::parse(&blob, ByteOrder::Little, GainTag::ProfileGainTableMap2)
            .expect("valid gamma");
        assert_eq!(map.gain_at(0, 0, 1, 1, [0.25, 0.0, 0.0]), Ok(1.375));
    }

    #[test]
    fn rejects_malformed_header_and_nonfinite_gains() {
        let good = fixture(ByteOrder::Little, GainTag::ProfileGainTableMap2, 3);
        assert_eq!(
            GainTable::parse(
                &good[..good.len() - 1],
                ByteOrder::Little,
                GainTag::ProfileGainTableMap2
            )
            .err(),
            Some(Error::Invalid)
        );
        let mut no_spacing = good.clone();
        no_spacing[8..16].fill(0);
        assert_eq!(
            GainTable::parse(
                &no_spacing,
                ByteOrder::Little,
                GainTag::ProfileGainTableMap2
            )
            .err(),
            Some(Error::Invalid)
        );
        let mut infinite = good;
        infinite[80..84].copy_from_slice(&f32::INFINITY.to_le_bytes());
        assert_eq!(
            GainTable::parse(&infinite, ByteOrder::Little, GainTag::ProfileGainTableMap2).err(),
            Some(Error::Invalid)
        );
    }

    #[test]
    fn configured_sdk_gain_boundaries() {
        let float_map = fixture(ByteOrder::Little, GainTag::ProfileGainTableMap2, 3);
        for (value, supported) in [
            (0.124f32, false),
            (0.125, true),
            (8.0, true),
            (8.001, false),
            (f32::NAN, false),
        ] {
            let mut data = float_map.clone();
            data[68..72].copy_from_slice(&value.to_le_bytes());
            assert_eq!(
                GainTable::parse(&data, ByteOrder::Little, GainTag::ProfileGainTableMap2).is_ok(),
                supported
            );
        }
        for (at, value, supported) in [
            (72, 0.0f32, false),
            (72, 1.0 / 4096.0, true),
            (72, 4097.0, true),
            (76, -4097.0, true),
            (76, 4096.0, true),
            (76, 4097.0, false),
            (76, f32::NAN, false),
            (80, 0.0, false),
            (80, 1.0 / 4096.0, true),
            (80, 4096.0, true),
            (80, 4097.0, false),
        ] {
            let mut data = float_map.clone();
            data[at..at + 4].copy_from_slice(&value.to_le_bytes());
            assert_eq!(
                GainTable::parse(&data, ByteOrder::Little, GainTag::ProfileGainTableMap2).is_ok(),
                supported,
                "offset {at}, value {value}"
            );
        }
        let mut integer_map = fixture(ByteOrder::Little, GainTag::ProfileGainTableMap2, 0);
        integer_map[72..76].copy_from_slice(&2.0f32.to_le_bytes());
        integer_map[76..80].copy_from_slice(&0.5f32.to_le_bytes());
        assert!(GainTable::parse(
            &integer_map,
            ByteOrder::Little,
            GainTag::ProfileGainTableMap2
        )
        .is_ok());
        integer_map[76..80].copy_from_slice(&0.0f32.to_le_bytes());
        assert_eq!(
            GainTable::parse(
                &integer_map,
                ByteOrder::Little,
                GainTag::ProfileGainTableMap2
            )
            .err(),
            Some(Error::Invalid)
        );
    }
}
