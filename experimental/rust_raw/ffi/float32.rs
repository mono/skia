// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Checked root-IFD, uncompressed RGB IEEE-float Stage 1. Identity Stage 2/3
//! requires separately checked processing metadata and finite sample values.

use super::dng::{
    identity_array, integral, number, optional, range, required, scalar, ByteOrder, Error, Tag,
};
use super::tiled::read_ifd;

struct Strip {
    offset: usize,
}

pub struct Plan {
    order: ByteOrder,
    width: u32,
    height: u32,
    rows_per_strip: u32,
    strips: Vec<Strip>,
    stage2_identity: bool,
    stage3_identity: bool,
}

pub struct Image {
    bytes: Vec<u8>,
    plan: Plan,
}

fn recognized_tag(tag: &Tag<'_>) -> Result<(), Error> {
    let kind = tag.kind;
    let valid = match tag.id {
        254 | 34665 | 50941 | 51090 | 51110 => kind == 4,
        256 | 257 | 273 | 278 | 279 | 50717 | 50829 => kind == 3 || kind == 4,
        258 | 259 | 262 | 274 | 277 | 284 | 339 | 50712 | 50713 | 50778 | 33421 => kind == 3,
        305 | 306 | 50708 | 50936 => kind == 2,
        50706 | 50707 | 50781 | 51111 => kind == 1,
        50714 | 50719 | 50720 => matches!(kind, 3 | 4 | 5),
        50718 | 50727 | 50728 | 50731 | 50732 | 50734 | 50738 | 50739 | 50780 | 51091 => kind == 5,
        50721 | 50730 | 50715 | 50716 | 50964 | 51109 => kind == 10,
        50940 => kind == 11,
        700 => kind == 1 || kind == 7,
        33422 => kind == 1,
        50933 => kind == 4,
        52544 | 51009 | 51022 => kind == 7,
        _ => return Err(Error::Unsupported),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

impl Plan {
    pub fn parse(data: &[u8]) -> Result<Option<Self>, Error> {
        let header = range(data, 0, 8)?;
        let order = match &header[..2] {
            b"II" => ByteOrder::Little,
            b"MM" => ByteOrder::Big,
            _ => return Ok(None),
        };
        if order.u16(&header[2..4]) != 42 {
            return Ok(None);
        }
        let first = order.u32(&header[4..8]) as usize;
        let count = order.u16(range(data, first, 2)?) as usize;
        let start = first.checked_add(2).ok_or(Error::Invalid)?;
        let entries = range(data, start, count.checked_mul(12).ok_or(Error::Invalid)?)?;
        if entries
            .chunks_exact(12)
            .any(|entry| order.u16(&entry[..2]) == 330)
        {
            return Ok(None); // Other readers validate SubIFD graphs.
        }
        let samples_are_rgb = entries.chunks_exact(12).any(|entry| {
            order.u16(&entry[..2]) == 277
                && order.u16(&entry[2..4]) == 3
                && order.u32(&entry[4..8]) == 1
                && order.u16(&entry[8..10]) == 3
        });
        if !samples_are_rgb {
            return Ok(None);
        }
        let Some(bits) = entries.chunks_exact(12).find(|entry| {
            order.u16(&entry[..2]) == 258
                && order.u16(&entry[2..4]) == 3
                && order.u32(&entry[4..8]) == 3
        }) else {
            return Ok(None);
        };
        let values = range(data, order.u32(&bits[8..12]) as usize, 6)?;
        if (0..3).any(|i| order.u16(&values[i * 2..i * 2 + 2]) != 32) {
            return Ok(None);
        }

        let ifd = read_ifd(data, first, order)?;
        if ifd.next != 0 {
            return Err(if ifd.next as usize == first {
                Error::Invalid
            } else {
                Error::Unsupported
            });
        }
        let tags = &ifd.tags;
        if optional(tags, 51008).is_some() {
            return Err(Error::Unsupported); // Stage-1-changing opcode.
        }
        for tag in tags {
            recognized_tag(tag)?;
        }
        let version = required(tags, 50706)?;
        let backward = required(tags, 50707)?;
        if version.count != 4 || backward.count != 4 {
            return Err(Error::Invalid);
        }
        if version.value != &[1, 7, 0, 0] || backward.value != &[1, 4, 0, 0] {
            return Err(Error::Unsupported);
        }
        let model = required(tags, 50708)?;
        if model.count < 2 || model.value.last() != Some(&0) {
            return Err(Error::Invalid);
        }
        let width = scalar(required(tags, 256)?, order)?;
        let height = scalar(required(tags, 257)?, order)?;
        if width == 0 || height == 0 || width > 300_000 || height > 300_000 {
            return Err(Error::Invalid);
        }
        for (id, expected) in [(254, 0), (259, 1), (262, 34892), (277, 3), (284, 1)] {
            if scalar(required(tags, id)?, order)? != expected {
                return Err(Error::Unsupported);
            }
        }
        let sample_format = required(tags, 339)?;
        if sample_format.count != 3 || (0..3).any(|i| number(sample_format, i, order) != Ok(3)) {
            return Err(Error::Unsupported);
        }
        if let Some(extra_profiles) = optional(tags, 50933) {
            for i in 0..extra_profiles.count as usize {
                let offset = number(extra_profiles, i, order)? as usize;
                if offset == 0 {
                    return Err(Error::Invalid);
                }
                range(data, offset, 2)?;
            }
        }

        let white_identity = optional(tags, 50717).is_some_and(|white| {
            white.count == 3 && (0..3).all(|i| number(white, i, order) == Ok(1))
        });
        let factors_identity = [50734, 50738, 50780].into_iter().all(|id| {
            optional(tags, id)
                .is_none_or(|value| value.count == 1 && integral(value, 0, order) == Ok(1))
        });
        let geometry_identity = optional(tags, 274).is_none_or(|o| scalar(o, order) == Ok(1))
            && optional(tags, 50718)
                .is_some_and(|_| identity_array(tags, 50718, &[1, 1], order).is_ok())
            && optional(tags, 50719)
                .is_some_and(|_| identity_array(tags, 50719, &[0, 0], order).is_ok())
            && optional(tags, 50720)
                .is_some_and(|_| identity_array(tags, 50720, &[width, height], order).is_ok())
            && identity_array(tags, 50829, &[0, 0, height, width], order).is_ok();
        let black_render_identity =
            optional(tags, 51110).is_none_or(|tag| tag.count == 1 && scalar(tag, order) == Ok(1));
        let processing_identity = white_identity
            && factors_identity
            && geometry_identity
            && black_render_identity
            && [50712, 50713, 50714, 50715, 50716, 33421, 33422, 51009]
                .into_iter()
                .all(|id| optional(tags, id).is_none());

        let rows_per_strip = scalar(required(tags, 278)?, order)?;
        if rows_per_strip == 0 {
            return Err(Error::Invalid);
        }
        let expected_count = 1 + (height - 1) / rows_per_strip;
        let offsets = required(tags, 273)?;
        let lengths = required(tags, 279)?;
        if offsets.count != expected_count || lengths.count != expected_count {
            return Err(Error::Invalid);
        }
        let row_bytes = (width as usize)
            .checked_mul(3)
            .and_then(|n| n.checked_mul(4))
            .ok_or(Error::Invalid)?;
        let mut finite_unit_samples = true;
        for index in 0..expected_count {
            let first_row = index.checked_mul(rows_per_strip).ok_or(Error::Invalid)?;
            let rows = (height - first_row).min(rows_per_strip);
            let required_bytes = (rows as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            if number(lengths, index as usize, order)? as usize != required_bytes {
                return Err(Error::Invalid);
            }
            let offset = number(offsets, index as usize, order)? as usize;
            let strip = range(data, offset, required_bytes)?;
            for sample in strip.chunks_exact(4) {
                let bits = order.u32(sample);
                let value = f32::from_bits(bits);
                if !value.is_finite() || !(0.0..=1.0).contains(&value) || bits == 0x80000000 {
                    finite_unit_samples = false;
                }
            }
        }
        let mut strips = Vec::new();
        strips
            .try_reserve_exact(expected_count as usize)
            .map_err(|_| Error::OutOfMemory)?;
        for index in 0..expected_count {
            strips.push(Strip {
                offset: number(offsets, index as usize, order)? as usize,
            });
        }
        Ok(Some(Self {
            order,
            width,
            height,
            rows_per_strip,
            strips,
            stage2_identity: processing_identity && finite_unit_samples,
            stage3_identity: processing_identity
                && finite_unit_samples
                && optional(tags, 51022).is_none(),
        }))
    }
}

impl Image {
    pub fn new(bytes: Vec<u8>, plan: Plan) -> Self {
        Self { bytes, plan }
    }
    pub fn width(&self) -> u32 {
        self.plan.width
    }
    pub fn height(&self) -> u32 {
        self.plan.height
    }

    pub fn stage2_status(&self) -> Result<(), Error> {
        if self.plan.stage2_identity {
            Ok(())
        } else {
            Err(Error::Unsupported)
        }
    }
    pub fn stage3_status(&self) -> Result<(), Error> {
        if self.plan.stage3_identity {
            Ok(())
        } else {
            Err(Error::Unsupported)
        }
    }
    pub fn stage2_row(&self, row: u32, output: &mut [f32]) -> Result<(), Error> {
        self.stage2_status()?;
        self.row(row, output)
    }
    pub fn stage3_row(&self, row: u32, output: &mut [f32]) -> Result<(), Error> {
        self.stage3_status()?;
        self.row(row, output)
    }
    pub fn row(&self, row: u32, output: &mut [f32]) -> Result<(), Error> {
        let expected = (self.plan.width as usize)
            .checked_mul(3)
            .ok_or(Error::Invalid)?;
        if row >= self.plan.height || output.len() != expected {
            return Err(Error::Invalid);
        }
        let bytes_per_row = expected.checked_mul(4).ok_or(Error::Invalid)?;
        let strip = &self.plan.strips[(row / self.plan.rows_per_strip) as usize];
        let offset = ((row % self.plan.rows_per_strip) as usize)
            .checked_mul(bytes_per_row)
            .and_then(|n| strip.offset.checked_add(n))
            .ok_or(Error::Invalid)?;
        let bytes = range(&self.bytes, offset, bytes_per_row)?;
        for (source, dest) in bytes.chunks_exact(4).zip(output.iter_mut()) {
            *dest = f32::from_bits(self.plan.order.u32(source));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{Error, Image, Plan};

    const FINITE: [u32; 27] = [
        0, 0x3f800000, 0x3f000000, 0x3e800000, 0x3e000000, 0x3d800000, 0x3f400000, 0x3f200000,
        0x3f600000, 0x3f000000, 0x3f000000, 0x3f000000, 0, 0x3f800000, 0x3f000000, 0x3e800000,
        0x3e000000, 0x3d800000, 0x3f400000, 0x3f200000, 0x3f600000, 0x3f000000, 0x3f000000,
        0x3f000000, 0, 0x3f800000, 0x3f000000,
    ];

    fn fixture(big_endian: bool, multi: bool, nonfinite: bool) -> Vec<u8> {
        let word = |n: u16| {
            if big_endian {
                n.to_be_bytes()
            } else {
                n.to_le_bytes()
            }
        };
        let long = |n: u32| {
            if big_endian {
                n.to_be_bytes()
            } else {
                n.to_le_bytes()
            }
        };
        let rational = |n: u32| [long(n), long(1)].concat();
        let pair = |a: u32, b: u32| [rational(a), rational(b)].concat();
        let mut offsets = if multi { vec![0; 8] } else { long(0).to_vec() };
        let sizes = if multi {
            [long(72), long(36)].concat()
        } else {
            long(108).to_vec()
        };
        let mut fields = vec![
            (254, 4, 1, long(0).to_vec()),
            (256, 4, 1, long(3).to_vec()),
            (257, 4, 1, long(3).to_vec()),
            (258, 3, 3, [word(32); 3].concat()),
            (259, 3, 1, word(1).to_vec()),
            (262, 3, 1, word(34892).to_vec()),
            (
                273,
                4,
                if multi { 2 } else { 1 },
                std::mem::take(&mut offsets),
            ),
            (274, 3, 1, word(1).to_vec()),
            (277, 3, 1, word(3).to_vec()),
            (278, 4, 1, long(if multi { 2 } else { 3 }).to_vec()),
            (279, 4, if multi { 2 } else { 1 }, sizes),
            (284, 3, 1, word(1).to_vec()),
            (339, 3, 3, [word(3); 3].concat()),
            (50706, 1, 4, vec![1, 7, 0, 0]),
            (50707, 1, 4, vec![1, 4, 0, 0]),
            (50708, 2, 6, b"Float\0".to_vec()),
            (50717, 3, 3, [word(1); 3].concat()),
            (50718, 5, 2, pair(1, 1)),
            (50719, 5, 2, pair(0, 0)),
            (50720, 5, 2, pair(3, 3)),
            (50738, 5, 1, rational(1)),
            (50780, 5, 1, rational(1)),
            (51109, 10, 1, [long((-2i32) as u32), long(1)].concat()),
            (51110, 4, 1, long(1).to_vec()),
            (52544, 7, 4, vec![0; 4]),
        ];
        fields.sort_unstable_by_key(|field| field.0);
        let mut bytes = if big_endian {
            b"MM\0\x2a\0\0\0\x08".to_vec()
        } else {
            b"II\x2a\0\x08\0\0\0".to_vec()
        };
        bytes.extend_from_slice(&word(fields.len() as u16));
        let start = bytes.len();
        bytes.resize(start + fields.len() * 12 + 4, 0);
        let mut strip_offsets = 0usize;
        let mut inline_offset = 0usize;
        for (i, (id, kind, count, value)) in fields.iter().enumerate() {
            let at = start + i * 12;
            bytes[at..at + 2].copy_from_slice(&word(*id));
            bytes[at + 2..at + 4].copy_from_slice(&word(*kind));
            bytes[at + 4..at + 8].copy_from_slice(&long(*count));
            if value.len() <= 4 {
                bytes[at + 8..at + 8 + value.len()].copy_from_slice(value);
                if *id == 273 {
                    inline_offset = at + 8;
                }
            } else {
                if bytes.len() % 2 != 0 {
                    bytes.push(0);
                }
                let offset = bytes.len();
                bytes[at + 8..at + 12].copy_from_slice(&long(offset as u32));
                bytes.extend_from_slice(value);
                if *id == 273 {
                    strip_offsets = offset;
                }
            }
        }
        let mut samples = FINITE;
        if nonfinite {
            samples[0] = 0x80000000; // signed zero
            samples[1] = 0x7fc12345; // quiet NaN with payload
            samples[2] = 0x7f800000; // positive infinity
            samples[3] = 0x7f812345; // signaling NaN with payload
        }
        for strip in 0..if multi { 2 } else { 1 } {
            if bytes.len() % 2 != 0 {
                bytes.push(0);
            }
            let at = if multi {
                strip_offsets + strip * 4
            } else {
                inline_offset
            };
            let offset = bytes.len() as u32;
            bytes[at..at + 4].copy_from_slice(&long(offset));
            let first = if strip == 0 { 0 } else { 18 };
            let end = if multi && strip == 0 { 18 } else { 27 };
            for bits in &samples[first..end] {
                bytes.extend_from_slice(&long(*bits));
            }
        }
        bytes
    }

    fn field(bytes: &[u8], id: u16) -> usize {
        let count = u16::from_le_bytes(bytes[8..10].try_into().expect("count"));
        (0..count as usize)
            .map(|i| 10 + i * 12)
            .find(|&at| u16::from_le_bytes(bytes[at..at + 2].try_into().expect("tag")) == id)
            .expect("tag")
    }

    #[test]
    fn preserves_exact_float_bits_across_endian_and_strips() {
        for big_endian in [false, true] {
            for multi in [false, true] {
                for nonfinite in [false, true] {
                    let bytes = fixture(big_endian, multi, nonfinite);
                    let plan = Plan::parse(&bytes)
                        .expect("checked TIFF")
                        .expect("float route");
                    let image = Image::new(bytes, plan);
                    assert_eq!((image.width(), image.height()), (3, 3));
                    assert_eq!(
                        image.stage2_status(),
                        if nonfinite {
                            Err(Error::Unsupported)
                        } else {
                            Ok(())
                        }
                    );
                    assert_eq!(
                        image.stage3_status(),
                        if nonfinite {
                            Err(Error::Unsupported)
                        } else {
                            Ok(())
                        }
                    );
                    for row in 0..3 {
                        let mut output = [f32::from_bits(0x7fcfffff); 9];
                        image.row(row, &mut output).expect("Stage 1");
                        let expected = if nonfinite {
                            let mut values = FINITE;
                            values[0] = 0x80000000;
                            values[1] = 0x7fc12345;
                            values[2] = 0x7f800000;
                            values[3] = 0x7f812345;
                            values
                        } else {
                            FINITE
                        };
                        for (index, sample) in output.iter().enumerate() {
                            assert_eq!(sample.to_bits(), expected[row as usize * 9 + index]);
                        }
                        if !nonfinite {
                            image
                                .stage2_row(row, &mut output)
                                .expect("identity Stage 2");
                            image
                                .stage3_row(row, &mut output)
                                .expect("identity Stage 3");
                            for (index, sample) in output.iter().enumerate() {
                                assert_eq!(sample.to_bits(), expected[row as usize * 9 + index]);
                            }
                        }
                    }
                    let sentinel = f32::from_bits(0x7fcfffff);
                    let mut short = [sentinel; 8];
                    assert_eq!(image.row(0, &mut short), Err(Error::Invalid));
                    assert!(short
                        .iter()
                        .all(|sample| sample.to_bits() == sentinel.to_bits()));
                    if nonfinite {
                        let mut output = [sentinel; 9];
                        assert_eq!(image.stage2_row(0, &mut output), Err(Error::Unsupported));
                        assert_eq!(image.stage3_row(0, &mut output), Err(Error::Unsupported));
                        assert!(output
                            .iter()
                            .all(|sample| sample.to_bits() == sentinel.to_bits()));
                    }
                }
            }
        }
    }

    #[test]
    fn rejects_bad_ranges_types_and_unverified_processing_without_losing_stage1() {
        let bytes = fixture(false, true, false);
        assert!(matches!(
            Plan::parse(&bytes[..bytes.len() - 1]),
            Err(Error::Incomplete)
        ));

        let mut outside = bytes.clone();
        let offsets_tag = field(&outside, 273);
        let offsets = u32::from_le_bytes(
            outside[offsets_tag + 8..offsets_tag + 12]
                .try_into()
                .expect("offsets"),
        ) as usize;
        outside[offsets + 4..offsets + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Plan::parse(&outside), Err(Error::Incomplete)));

        let mut wrong_count = bytes.clone();
        wrong_count[offsets_tag + 4..offsets_tag + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Plan::parse(&wrong_count), Err(Error::Invalid)));

        let mut wrong_type = bytes.clone();
        let format = field(&wrong_type, 339);
        wrong_type[format + 2..format + 4].copy_from_slice(&4u16.to_le_bytes());
        assert!(matches!(
            Plan::parse(&wrong_type),
            Err(Error::Invalid | Error::Incomplete)
        ));

        let mut opcode = bytes.clone();
        let profile = field(&opcode, 52544);
        opcode[profile..profile + 2].copy_from_slice(&51009u16.to_le_bytes());
        let plan = Plan::parse(&opcode).expect("Stage 1").expect("float route");
        let image = Image::new(opcode, plan);
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut raw = [0.0; 9];
        image.row(0, &mut raw).expect("Stage 1 preserved");
        assert_eq!(raw[1].to_bits(), FINITE[1]);
        let mut sentinel = [f32::from_bits(0x7fcfffff); 9];
        assert_eq!(image.stage2_row(0, &mut sentinel), Err(Error::Unsupported));
        assert!(sentinel.iter().all(|sample| sample.to_bits() == 0x7fcfffff));

        let mut opcode3 = bytes;
        opcode3[profile..profile + 2].copy_from_slice(&51022u16.to_le_bytes());
        let plan = Plan::parse(&opcode3)
            .expect("Stage 1")
            .expect("float route");
        let image = Image::new(opcode3, plan);
        assert_eq!(image.stage2_status(), Ok(()));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
    }

    #[test]
    fn finite_range_and_signed_zero_are_independent_identity_gates() {
        for bits in [0x80000000u32, 0x40000000, 0xbe800000] {
            let mut bytes = fixture(false, true, false);
            let tag = field(&bytes, 273);
            let offsets = u32::from_le_bytes(
                bytes[tag + 8..tag + 12]
                    .try_into()
                    .expect("strip offsets pointer"),
            ) as usize;
            let first =
                u32::from_le_bytes(bytes[offsets..offsets + 4].try_into().expect("first strip"))
                    as usize;
            bytes[first..first + 4].copy_from_slice(&bits.to_le_bytes());
            let plan = Plan::parse(&bytes)
                .expect("valid Stage 1")
                .expect("float route");
            let image = Image::new(bytes, plan);
            let mut raw = [0f32; 9];
            image.row(0, &mut raw).expect("bit-exact row");
            assert_eq!(raw[0].to_bits(), bits);
            assert_eq!(image.stage2_status(), Err(Error::Unsupported));
            assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        }
    }
}
