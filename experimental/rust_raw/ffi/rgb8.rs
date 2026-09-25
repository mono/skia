// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Checked root-IFD 8-bit RGB LinearRaw strips at Stage 1. Output-referred
//! Stage 2/3 includes bounded linearization; final color needs tighter guards.

use super::dng::{
    identity_array, integral, number, optional, range, required, scalar, srgb_from_linear_u8,
    ByteOrder, Error, Tag,
};
use super::gain_map::{GainTable, GainTag};
use super::linearization::{linearized_sample, LinearizationTable};
use super::tiled::read_ifd;

struct Strip {
    offset: usize,
    size: usize,
}

pub struct Plan {
    width: u32,
    height: u32,
    compression: u16,
    predictor: u16,
    rows_per_strip: u32,
    strips: Vec<Strip>,
    linearization: Option<LinearizationTable>,
    stage2_supported: bool,
    stage3_supported: bool,
    final_render_supported: bool,
}

pub struct Image {
    bytes: Vec<u8>,
    plan: Plan,
}

fn recognized_tag(tag: &Tag<'_>) -> Result<(), Error> {
    let kind = tag.kind;
    let valid = match tag.id {
        254 | 34665 | 50941 | 51110 => kind == 4,
        256 | 257 | 273 | 278 | 279 | 50717 | 50829 => kind == 3 || kind == 4,
        258 | 259 | 262 | 274 | 277 | 284 | 317 | 339 | 50712 | 50713 | 50778 | 50779 | 50879
        | 33421 => kind == 3,
        305 | 306 | 50708 | 50936 => kind == 2,
        50706 | 50707 | 50781 | 51111 => kind == 1,
        50714 | 50719 | 50720 => matches!(kind, 3 | 4 | 5),
        50718 | 50727 | 50728 | 50731 | 50732 | 50734 | 50738 | 50739 | 50780 => kind == 5,
        50721 | 50722 | 50723 | 50724 | 50730 | 50715 | 50716 | 50964 | 50965 | 51109 => kind == 10,
        50940 => kind == 11,
        700 => kind == 1 || kind == 7,
        33422 => kind == 1,
        50933 => kind == 4,
        52525 | 52544 | 52548 | 52550 | 51009 | 51022 => kind == 7,
        _ => return Err(Error::Unsupported),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

const SRGB_FORWARD_16_16: [i32; 9] = [28578, 25241, 9376, 14581, 46981, 3972, 912, 6362, 46799];
const CAMERA_MATRIX_MILLI: [i32; 9] = [1037, 0, 0, 0, 1000, 0, 0, 0, 1212];
const RGB8_FINAL_TAGS: &[u16] = &[
    254, 256, 257, 258, 259, 262, 273, 274, 277, 278, 279, 284, 317, 339, 50706, 50707, 50708,
    50712, 50713, 50714, 50717, 50718, 50719, 50720, 50721, 50728, 50778, 50829, 50879, 50940,
    50964, 51110, 52525, 52544,
];

fn matches_matrix(
    tags: &[Tag<'_>],
    id: u16,
    values: &[i32; 9],
    divisor: i32,
    order: ByteOrder,
) -> bool {
    optional(tags, id).is_some_and(|matrix| {
        matrix.kind == 10
            && matrix.count == 9
            && matrix
                .value
                .chunks_exact(8)
                .enumerate()
                .all(|(index, component)| {
                    let numerator = order.u32(&component[..4]) as i32;
                    let denominator = order.u32(&component[4..]) as i32;
                    denominator > 0
                        && i64::from(numerator) * i64::from(divisor)
                            == i64::from(values[index]) * i64::from(denominator)
                })
    })
}

fn identity_gain_map(tags: &[Tag<'_>], order: ByteOrder, width: u32, height: u32) -> bool {
    let gain = match (optional(tags, 52525), optional(tags, 52544)) {
        (None, None) => return true,
        (Some(_), Some(_)) => return false,
        (Some(tag), None) => (tag, GainTag::ProfileGainTableMap),
        (None, Some(tag)) => (tag, GainTag::ProfileGainTableMap2),
    };
    let Ok(table) = GainTable::parse(gain.0.value, order, gain.1) else {
        return false;
    };
    table.is_identity()
        && table.gain_at(0, 0, width, height, [0.0; 3]) == Ok(1.0)
        && table.gain_at(width - 1, height - 1, width, height, [1.0; 3]) == Ok(1.0)
}

fn identity_srgb_profile(tags: &[Tag<'_>], order: ByteOrder, width: u32, height: u32) -> bool {
    if !tags
        .iter()
        .all(|tag| RGB8_FINAL_TAGS.binary_search(&tag.id).is_ok())
        || !matches_matrix(tags, 50721, &CAMERA_MATRIX_MILLI, 1000, order)
        || !matches_matrix(tags, 50964, &SRGB_FORWARD_16_16, 65536, order)
        || !optional(tags, 50728).is_some_and(|tag| {
            tag.kind == 5 && tag.count == 3 && (0..3).all(|i| integral(tag, i, order) == Ok(1))
        })
        || !optional(tags, 50778).is_some_and(|tag| scalar(tag, order) == Ok(21))
        || !optional(tags, 51110).is_some_and(|tag| scalar(tag, order) == Ok(1))
    {
        return false;
    }
    optional(tags, 50940).is_some_and(|tone| {
        tone.kind == 11
            && tone.count == 4
            && tone
                .value
                .chunks_exact(4)
                .zip([0u32, 0, 1f32.to_bits(), 1f32.to_bits()])
                .all(|(value, expected)| order.u32(value) == expected)
    }) && identity_gain_map(tags, order, width, height)
}

impl Plan {
    pub fn parse(data: &[u8]) -> Result<Option<Self>, Error> {
        Self::parse_layout(data, 1)
    }

    pub fn parse_deflate(data: &[u8]) -> Result<Option<Self>, Error> {
        Self::parse_layout(data, 8)
    }

    fn parse_layout(data: &[u8], compression: u16) -> Result<Option<Self>, Error> {
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
            return Ok(None);
        }
        let scalar_is = |id: u16, expected: u16| {
            entries.chunks_exact(12).any(|entry| {
                order.u16(&entry[..2]) == id
                    && order.u16(&entry[2..4]) == 3
                    && order.u32(&entry[4..8]) == 1
                    && order.u16(&entry[8..10]) == expected
            })
        };
        if !scalar_is(277, 3) || !scalar_is(259, compression) {
            return Ok(None); // Do not seize SOF3, tiled, or mono inputs.
        }
        let Some(bits) = entries.chunks_exact(12).find(|entry| {
            order.u16(&entry[..2]) == 258
                && order.u16(&entry[2..4]) == 3
                && order.u32(&entry[4..8]) == 3
        }) else {
            return Ok(None);
        };
        let samples = range(data, order.u32(&bits[8..12]) as usize, 6)?;
        if (0..3).any(|i| order.u16(&samples[i * 2..i * 2 + 2]) != 8) {
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
        if optional(&ifd.tags, 51008).is_some() {
            return Err(Error::Unsupported); // OpcodeList1 alters Stage 1.
        }
        let tags = &ifd.tags;
        for tag in tags {
            recognized_tag(tag)?;
        }
        let version = required(tags, 50706)?;
        let backward = required(tags, 50707)?;
        if version.count != 4 || backward.count != 4 {
            return Err(Error::Invalid);
        }
        if version.value != &[1, 7, 0, 0] || backward.value != &[1, 1, 0, 0] {
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
        for (id, expected) in [
            (254, 0),
            (259, u32::from(compression)),
            (262, 34892),
            (277, 3),
            (284, 1),
        ] {
            if scalar(required(tags, id)?, order)? != expected {
                return Err(Error::Unsupported);
            }
        }
        let predictor = optional(tags, 317)
            .map(|tag| scalar(tag, order))
            .transpose()?
            .unwrap_or(1);
        if predictor != 1 && !(compression == 8 && predictor == 2) {
            return Err(Error::Unsupported);
        }
        if let Some(format) = optional(tags, 339) {
            if format.count != 3 || (0..3).any(|i| number(format, i, order) != Ok(1)) {
                return Err(Error::Unsupported);
            }
        }
        let linearization = optional(tags, 50712)
            .map(|tag| LinearizationTable::parse(tag, order))
            .transpose()?;
        if let Some(extra_profiles) = optional(tags, 50933) {
            for index in 0..extra_profiles.count as usize {
                let offset = number(extra_profiles, index, order)? as usize;
                if offset == 0 {
                    return Err(Error::Invalid);
                }
                range(data, offset, 2)?;
            }
        }
        let output_referred =
            optional(tags, 50879).is_some_and(|tag| tag.count == 1 && scalar(tag, order) == Ok(1));
        let black_repeat_identity = optional(tags, 50713)
            .is_some_and(|_| identity_array(tags, 50713, &[1, 1], order).is_ok());
        let black_identity = optional(tags, 50714).is_some_and(|level| {
            level.count == 3 && (0..3).all(|i| integral(level, i, order) == Ok(0))
        });
        let white_identity = optional(tags, 50717).is_some_and(|level| {
            level.count == 3 && (0..3).all(|i| number(level, i, order) == Ok(255))
        });
        let geometry_identity = optional(tags, 274).is_none_or(|tag| scalar(tag, order) == Ok(1))
            && optional(tags, 50718)
                .is_some_and(|_| identity_array(tags, 50718, &[1, 1], order).is_ok())
            && optional(tags, 50719)
                .is_some_and(|_| identity_array(tags, 50719, &[0, 0], order).is_ok())
            && optional(tags, 50720)
                .is_some_and(|_| identity_array(tags, 50720, &[width, height], order).is_ok())
            && identity_array(tags, 50829, &[0, 0, height, width], order).is_ok();
        let factors_identity = [50734, 50738, 50780].into_iter().all(|id| {
            optional(tags, id).is_none_or(|tag| tag.count == 1 && integral(tag, 0, order) == Ok(1))
        }) && optional(tags, 51110)
            .is_none_or(|tag| tag.count == 1 && scalar(tag, order) == Ok(1));
        let stage2_supported = output_referred
            && black_repeat_identity
            && black_identity
            && white_identity
            && geometry_identity
            && factors_identity
            && linearization
                .as_ref()
                .is_none_or(LinearizationTable::maps_to_sdr_8_bit)
            && [50715, 50716, 33421, 33422, 51009]
                .into_iter()
                .all(|id| optional(tags, id).is_none());
        let stage3_supported = stage2_supported && optional(tags, 51022).is_none();
        let final_render_supported =
            stage3_supported && identity_srgb_profile(tags, order, width, height);

        let rows_per_strip = scalar(required(tags, 278)?, order)?;
        if rows_per_strip == 0 {
            return Err(Error::Invalid);
        }
        let strip_count = 1 + (height - 1) / rows_per_strip;
        let offsets = required(tags, 273)?;
        let lengths = required(tags, 279)?;
        if offsets.count != strip_count || lengths.count != strip_count {
            return Err(Error::Invalid);
        }
        let row_bytes = (width as usize).checked_mul(3).ok_or(Error::Invalid)?;
        for index in 0..strip_count {
            let first_row = index.checked_mul(rows_per_strip).ok_or(Error::Invalid)?;
            let rows = (height - first_row).min(rows_per_strip);
            let required_size = (rows as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            let size = number(lengths, index as usize, order)? as usize;
            if size == 0 || compression == 1 && size != required_size {
                return Err(Error::Invalid);
            }
            let offset = number(offsets, index as usize, order)? as usize;
            range(data, offset, size)?;
        }
        let mut strips = Vec::new();
        strips
            .try_reserve_exact(strip_count as usize)
            .map_err(|_| Error::OutOfMemory)?;
        for index in 0..strip_count {
            strips.push(Strip {
                offset: number(offsets, index as usize, order)? as usize,
                size: number(lengths, index as usize, order)? as usize,
            });
        }
        Ok(Some(Self {
            width,
            height,
            compression,
            predictor: predictor as u16,
            rows_per_strip,
            strips,
            linearization,
            stage2_supported,
            stage3_supported,
            final_render_supported,
        }))
    }
}

impl Image {
    pub fn new(bytes: Vec<u8>, plan: Plan) -> Self {
        Self { bytes, plan }
    }

    pub fn inflate(
        bytes: Vec<u8>,
        mut plan: Plan,
        mut validate: impl FnMut(&[u8], usize) -> Result<(), Error>,
        mut decompress: impl FnMut(&[u8], &mut [u8]) -> Result<(), Error>,
    ) -> Result<Self, Error> {
        if plan.compression != 8 {
            return Err(Error::Unsupported);
        }
        let row_bytes = (plan.width as usize).checked_mul(3).ok_or(Error::Invalid)?;
        let total_bytes = (plan.height as usize)
            .checked_mul(row_bytes)
            .ok_or(Error::Invalid)?;
        for (index, strip) in plan.strips.iter().enumerate() {
            let first_row = index
                .checked_mul(plan.rows_per_strip as usize)
                .ok_or(Error::Invalid)?;
            let remaining = (plan.height as usize)
                .checked_sub(first_row)
                .ok_or(Error::Invalid)?;
            let size = remaining
                .min(plan.rows_per_strip as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            validate(range(&bytes, strip.offset, strip.size)?, size)?;
        }
        let mut decoded = Vec::new();
        decoded
            .try_reserve_exact(total_bytes)
            .map_err(|_| Error::OutOfMemory)?;
        decoded.resize(total_bytes, 0);
        for (index, strip) in plan.strips.iter_mut().enumerate() {
            let first_row = index
                .checked_mul(plan.rows_per_strip as usize)
                .ok_or(Error::Invalid)?;
            let remaining = (plan.height as usize)
                .checked_sub(first_row)
                .ok_or(Error::Invalid)?;
            let size = remaining
                .min(plan.rows_per_strip as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            let start = first_row.checked_mul(row_bytes).ok_or(Error::Invalid)?;
            let output = decoded
                .get_mut(start..start.checked_add(size).ok_or(Error::Invalid)?)
                .ok_or(Error::Invalid)?;
            decompress(range(&bytes, strip.offset, strip.size)?, output)?;
            if plan.predictor == 2 {
                for row in output.chunks_exact_mut(row_bytes) {
                    let mut previous = [0u8; 3];
                    for (index, sample) in row.iter_mut().enumerate() {
                        let lane = index % 3;
                        *sample = sample.wrapping_add(previous[lane]);
                        previous[lane] = *sample;
                    }
                }
            }
            strip.offset = start;
            strip.size = size;
        }
        Ok(Self {
            bytes: decoded,
            plan,
        })
    }
    pub fn width(&self) -> u32 {
        self.plan.width
    }
    pub fn height(&self) -> u32 {
        self.plan.height
    }
    pub fn stage2_status(&self) -> Result<(), Error> {
        if self.plan.stage2_supported {
            Ok(())
        } else {
            Err(Error::Unsupported)
        }
    }
    pub fn stage3_status(&self) -> Result<(), Error> {
        if self.plan.stage3_supported {
            Ok(())
        } else {
            Err(Error::Unsupported)
        }
    }
    pub fn supports_final_render(&self) -> bool {
        self.plan.final_render_supported
    }
    fn source_row(&self, row: u32) -> Result<&[u8], Error> {
        if row >= self.plan.height {
            return Err(Error::Invalid);
        }
        let stride = (self.plan.width as usize)
            .checked_mul(3)
            .ok_or(Error::Invalid)?;
        let strip = &self.plan.strips[(row / self.plan.rows_per_strip) as usize];
        let offset = ((row % self.plan.rows_per_strip) as usize)
            .checked_mul(stride)
            .and_then(|n| strip.offset.checked_add(n))
            .ok_or(Error::Invalid)?;
        range(&self.bytes, offset, stride)
    }
    pub fn raw_row(&self, row: u32, output: &mut [u8]) -> Result<(), Error> {
        let expected = (self.plan.width as usize)
            .checked_mul(3)
            .ok_or(Error::Invalid)?;
        if output.len() != expected {
            return Err(Error::Invalid);
        }
        output.copy_from_slice(self.source_row(row)?);
        Ok(())
    }
    pub fn stage2_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage2_status()?;
        let expected = (self.plan.width as usize)
            .checked_mul(3)
            .ok_or(Error::Invalid)?;
        if output.len() != expected {
            return Err(Error::Invalid);
        }
        let input = self.source_row(row)?;
        for (&source, dest) in input.iter().zip(output.iter_mut()) {
            *dest = linearized_sample(self.plan.linearization.as_ref(), u16::from(source)) * 257;
        }
        Ok(())
    }
    pub fn stage3_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage3_status()?;
        self.stage2_row(row, output)
    }

    pub fn copy_rgb_row(&self, row: u32, output: &mut [u8]) -> Result<(), Error> {
        if !self.supports_final_render() {
            return Err(Error::Unsupported);
        }
        let source = self.source_row(row)?;
        if output.len() != source.len() {
            return Err(Error::Invalid);
        }
        for (&sample, converted) in source.iter().zip(output.iter_mut()) {
            let mapped = linearized_sample(self.plan.linearization.as_ref(), u16::from(sample));
            // Final-render gating ensures every mapped sample fits in a byte.
            *converted = srgb_from_linear_u8(mapped as u8);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{
        srgb_from_linear_u8, Error, Image, Plan, CAMERA_MATRIX_MILLI, RGB8_FINAL_TAGS,
        SRGB_FORWARD_16_16,
    };
    use std::cell::Cell;

    const PIXELS: [u8; 27] = [
        0, 0, 0, 1, 1, 1, 255, 255, 255, 255, 0, 0, 128, 0, 127, 0, 0, 255, 0, 1, 0, 0, 128, 0, 0,
        255, 0,
    ];

    fn fixture(big_endian: bool, multi: bool) -> Vec<u8> {
        fixture_with_srgb(big_endian, multi, false)
    }

    fn fixture_with_srgb(big_endian: bool, multi: bool, srgb_profile: bool) -> Vec<u8> {
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
        let mut matrix = Vec::new();
        let mut forward = Vec::new();
        for i in 0..9 {
            if srgb_profile {
                matrix.extend_from_slice(
                    &[
                        long(CAMERA_MATRIX_MILLI[i].try_into().expect("positive matrix")),
                        long(1000),
                    ]
                    .concat(),
                );
                forward.extend_from_slice(
                    &[
                        long(SRGB_FORWARD_16_16[i].try_into().expect("positive matrix")),
                        long(65536),
                    ]
                    .concat(),
                );
            } else {
                let identity = rational(if i % 4 == 0 { 1 } else { 0 });
                matrix.extend_from_slice(&identity);
                forward.extend_from_slice(&identity);
            }
        }
        let mut area = Vec::new();
        for value in [0, 0, 3, 3] {
            area.extend_from_slice(&long(value));
        }
        let mut fields = vec![
            (254, 4, 1, long(0).to_vec()),
            (256, 4, 1, long(3).to_vec()),
            (257, 4, 1, long(3).to_vec()),
            (258, 3, 3, [word(8); 3].concat()),
            (259, 3, 1, word(1).to_vec()),
            (262, 3, 1, word(34892).to_vec()),
            (
                273,
                4,
                if multi { 2 } else { 1 },
                if multi { vec![0; 8] } else { long(0).to_vec() },
            ),
            (274, 3, 1, word(1).to_vec()),
            (277, 3, 1, word(3).to_vec()),
            (278, 4, 1, long(if multi { 2 } else { 3 }).to_vec()),
            (
                279,
                4,
                if multi { 2 } else { 1 },
                if multi {
                    [long(18), long(9)].concat()
                } else {
                    long(27).to_vec()
                },
            ),
            (284, 3, 1, word(1).to_vec()),
            (339, 3, 3, [word(1); 3].concat()),
            (50706, 1, 4, vec![1, 7, 0, 0]),
            (50707, 1, 4, vec![1, 1, 0, 0]),
            (50708, 2, 5, b"RGB8\0".to_vec()),
            (50713, 3, 2, [word(1); 2].concat()),
            (
                50714,
                5,
                3,
                [rational(0), rational(0), rational(0)].concat(),
            ),
            (50717, 3, 3, [word(255); 3].concat()),
            (50718, 5, 2, pair(1, 1)),
            (50719, 5, 2, pair(0, 0)),
            (50720, 5, 2, pair(3, 3)),
            (50721, 10, 9, matrix.clone()),
            (
                50728,
                5,
                3,
                [rational(1), rational(1), rational(1)].concat(),
            ),
            (50778, 3, 1, word(21).to_vec()),
            (50829, 4, 4, area),
            (50879, 3, 1, word(1).to_vec()),
            (
                50940,
                11,
                4,
                [0f32.to_bits(), 0, 1f32.to_bits(), 1f32.to_bits()]
                    .into_iter()
                    .flat_map(long)
                    .collect(),
            ),
            (50964, 10, 9, forward),
            (51110, 4, 1, long(1).to_vec()),
        ];
        fields.sort_unstable_by_key(|field| field.0);
        let mut bytes = if big_endian {
            b"MM\0\x2a\0\0\0\x08".to_vec()
        } else {
            b"II\x2a\0\x08\0\0\0".to_vec()
        };
        bytes.extend_from_slice(&word(fields.len() as u16));
        let table = bytes.len();
        bytes.resize(table + fields.len() * 12 + 4, 0);
        let mut strip_offsets = 0usize;
        let mut inline = 0usize;
        for (i, (tag, kind, count, value)) in fields.iter().enumerate() {
            let at = table + 12 * i;
            bytes[at..at + 2].copy_from_slice(&word(*tag));
            bytes[at + 2..at + 4].copy_from_slice(&word(*kind));
            bytes[at + 4..at + 8].copy_from_slice(&long(*count));
            if value.len() <= 4 {
                bytes[at + 8..at + 8 + value.len()].copy_from_slice(value);
                if *tag == 273 {
                    inline = at + 8;
                }
            } else {
                if bytes.len() % 2 != 0 {
                    bytes.push(0);
                }
                let offset = bytes.len();
                bytes[at + 8..at + 12].copy_from_slice(&long(offset as u32));
                bytes.extend_from_slice(value);
                if *tag == 273 {
                    strip_offsets = offset;
                }
            }
        }
        for strip in 0..if multi { 2 } else { 1 } {
            if bytes.len() % 2 != 0 {
                bytes.push(0);
            }
            let at = if multi {
                strip_offsets + strip * 4
            } else {
                inline
            };
            let offset = bytes.len() as u32;
            bytes[at..at + 4].copy_from_slice(&long(offset));
            let start = if strip == 0 { 0 } else { 18 };
            let end = if multi && strip == 0 { 18 } else { 27 };
            bytes.extend_from_slice(&PIXELS[start..end]);
        }
        bytes
    }

    fn deflate_fixture(big_endian: bool, multi: bool, predictor: u16) -> Vec<u8> {
        let mut bytes = fixture_with_srgb(big_endian, multi, true);
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
        let count = if big_endian {
            u16::from_be_bytes(bytes[8..10].try_into().expect("count"))
        } else {
            u16::from_le_bytes(bytes[8..10].try_into().expect("count"))
        };
        let find = |id: u16, data: &[u8]| {
            (0..count as usize)
                .map(|i| 10 + 12 * i)
                .find(|&at| data[at..at + 2] == word(id))
                .expect("known tag")
        };
        let compression = find(259, &bytes);
        bytes[compression + 8..compression + 10].copy_from_slice(&word(8));
        let format = find(339, &bytes);
        bytes[format..format + 12].copy_from_slice(
            &[
                word(317).as_slice(),
                word(3).as_slice(),
                long(1).as_slice(),
                word(predictor).as_slice(),
                &[0, 0],
            ]
            .concat(),
        );
        bytes
    }

    fn linearized_fixture(big_endian: bool, multi: bool, compressed: bool, short: bool) -> Vec<u8> {
        let mut bytes = if compressed {
            deflate_fixture(big_endian, multi, 2)
        } else {
            fixture_with_srgb(big_endian, multi, true)
        };
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
        let count = if big_endian {
            u16::from_be_bytes(bytes[8..10].try_into().expect("IFD count"))
        } else {
            u16::from_le_bytes(bytes[8..10].try_into().expect("IFD count"))
        };
        let mut fields: Vec<[u8; 12]> = bytes[10..10 + 12 * count as usize]
            .chunks_exact(12)
            .map(|entry| entry.try_into().expect("IFD tag"))
            .collect();
        let value = if short {
            [word(0), word(u16::from(u8::MAX))].concat()
        } else {
            if bytes.len() & 1 != 0 {
                bytes.push(0);
            }
            let offset = bytes.len();
            for sample in 0..=u8::MAX {
                bytes.extend_from_slice(&word(u16::from(sample).saturating_mul(2).min(255)));
            }
            long(offset as u32).to_vec()
        };
        let tag: [u8; 12] = [
            word(50712).as_slice(),
            word(3).as_slice(),
            long(if short { 2 } else { 256 }).as_slice(),
            value.as_slice(),
        ]
        .concat()
        .try_into()
        .expect("linearization tag");
        fields.push(tag);
        fields.sort_by_key(|entry| {
            if big_endian {
                u16::from_be_bytes(entry[..2].try_into().expect("tag"))
            } else {
                u16::from_le_bytes(entry[..2].try_into().expect("tag"))
            }
        });
        if bytes.len() & 1 != 0 {
            bytes.push(0);
        }
        let ifd = bytes.len() as u32;
        bytes[4..8].copy_from_slice(&long(ifd));
        bytes.extend_from_slice(&word(fields.len() as u16));
        for entry in fields {
            bytes.extend_from_slice(&entry);
        }
        bytes.extend_from_slice(&long(0));
        bytes
    }

    fn decode_predicted_rgb8(encoded: &[u8], output: &mut [u8], predictor: u16) {
        assert_eq!(encoded.len(), output.len());
        for (source, dest) in encoded.chunks_exact(9).zip(output.chunks_exact_mut(9)) {
            let mut previous = [0u8; 3];
            for (index, &sample) in source.iter().enumerate() {
                let lane = index % 3;
                dest[index] = if predictor == 2 {
                    sample.wrapping_sub(previous[lane])
                } else {
                    sample
                };
                previous[lane] = sample;
            }
        }
    }

    #[test]
    fn deflate_srgb_strips_preflight_and_render() {
        for big_endian in [false, true] {
            for multi in [false, true] {
                for predictor in [1, 2] {
                    let bytes = deflate_fixture(big_endian, multi, predictor);
                    assert!(Plan::parse(&bytes).expect("uncompressed route").is_none());
                    let plan = Plan::parse_deflate(&bytes)
                        .expect("checked Deflate metadata")
                        .expect("RGB8 profile");
                    let checks = Cell::new(0);
                    let decodes = Cell::new(0);
                    let image = Image::inflate(
                        bytes,
                        plan,
                        |encoded, expected| {
                            assert_eq!(encoded.len(), expected);
                            checks.set(checks.get() + 1);
                            Ok(())
                        },
                        |encoded, output| {
                            assert_eq!(checks.get(), if multi { 2 } else { 1 });
                            decode_predicted_rgb8(encoded, output, predictor);
                            decodes.set(decodes.get() + 1);
                            Ok(())
                        },
                    )
                    .expect("preflighted RGB8 Deflate");
                    assert_eq!(
                        (checks.get(), decodes.get()),
                        (if multi { 2 } else { 1 }, if multi { 2 } else { 1 })
                    );
                    assert!(image.supports_final_render());
                    assert_eq!(image.bytes.len(), PIXELS.len());
                    for y in 0..3 {
                        let mut first = [0xa5; 10];
                        let mut rgb = [0xa5; 10];
                        image.raw_row(y, &mut first[..9]).expect("Stage 1");
                        image.copy_rgb_row(y, &mut rgb[..9]).expect("final pixels");
                        assert_eq!(first[..9], PIXELS[y as usize * 9..(y + 1) as usize * 9]);
                        for i in 0..9 {
                            assert_eq!(rgb[i], srgb_from_linear_u8(first[i]));
                        }
                        assert_eq!((first[9], rgb[9]), (0xa5, 0xa5));
                    }
                }
            }
        }
    }

    #[test]
    fn deflate_srgb_rejects_bad_predictors_and_preflight() {
        let mut bytes = deflate_fixture(false, true, 3);
        assert!(matches!(
            Plan::parse_deflate(&bytes),
            Err(Error::Unsupported)
        ));
        let predictor = field(&bytes, 317);
        bytes[predictor + 2] = 4;
        assert!(matches!(Plan::parse_deflate(&bytes), Err(Error::Invalid)));
        let bytes = deflate_fixture(false, true, 2);
        let plan = Plan::parse_deflate(&bytes)
            .expect("checked Deflate metadata")
            .expect("RGB8");
        let checks = Cell::new(0);
        let decodes = Cell::new(0);
        assert_eq!(
            Image::inflate(
                bytes,
                plan,
                |_, _| {
                    checks.set(checks.get() + 1);
                    if checks.get() == 2 {
                        Err(Error::Incomplete)
                    } else {
                        Ok(())
                    }
                },
                |_, _| {
                    decodes.set(decodes.get() + 1);
                    Ok(())
                },
            )
            .err(),
            Some(Error::Incomplete)
        );
        assert_eq!((checks.get(), decodes.get()), (2, 0));
    }

    #[test]
    fn linearization_table_maps_rgb8_stages_and_final_color() {
        for big_endian in [false, true] {
            for multi in [false, true] {
                for compressed in [false, true] {
                    for short in [false, true] {
                        let bytes = linearized_fixture(big_endian, multi, compressed, short);
                        let plan = if compressed {
                            Plan::parse_deflate(&bytes)
                        } else {
                            Plan::parse(&bytes)
                        }
                        .expect("valid table")
                        .expect("strict sRGB");
                        let image = if compressed {
                            Image::inflate(
                                bytes,
                                plan,
                                |encoded, expected| {
                                    assert_eq!(encoded.len(), expected);
                                    Ok(())
                                },
                                |encoded, output| {
                                    decode_predicted_rgb8(encoded, output, 2);
                                    Ok(())
                                },
                            )
                            .expect("Deflate image")
                        } else {
                            Image::new(bytes, plan)
                        };
                        assert!(image.supports_final_render());
                        assert_eq!(image.stage2_status(), Ok(()));
                        assert_eq!(image.stage3_status(), Ok(()));
                        for y in 0..3 {
                            let mut raw = [0xa5; 10];
                            let mut normalized = [0xa5a5; 10];
                            let mut srgb = [0xa5; 10];
                            image.raw_row(y, &mut raw[..9]).expect("Stage1");
                            image.stage2_row(y, &mut normalized[..9]).expect("Stage2");
                            image.copy_rgb_row(y, &mut srgb[..9]).expect("final RGB");
                            assert_eq!(raw[..9], PIXELS[y as usize * 9..(y + 1) as usize * 9]);
                            for i in 0..9 {
                                let mapped = if short {
                                    if raw[i] == 0 {
                                        0
                                    } else {
                                        u8::MAX
                                    }
                                } else {
                                    raw[i].saturating_mul(2)
                                };
                                assert_eq!(normalized[i], u16::from(mapped) * 257);
                                assert_eq!(srgb[i], srgb_from_linear_u8(mapped));
                            }
                            assert_eq!((raw[9], normalized[9], srgb[9]), (0xa5, 0xa5a5, 0xa5));
                            let expected = normalized;
                            normalized.fill(0xa5a5);
                            image.stage3_row(y, &mut normalized[..9]).expect("Stage3");
                            assert_eq!(normalized[..9], expected[..9]);
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn unverified_rgb8_linearization_stays_stage1_only() {
        let good = linearized_fixture(false, false, false, false);
        let first = u32::from_le_bytes(good[4..8].try_into().expect("IFD")) as usize;
        let count = u16::from_le_bytes(good[first..first + 2].try_into().expect("count")) as usize;
        let table = (0..count)
            .map(|i| first + 2 + i * 12)
            .find(|&at| good[at..at + 2] == 50712u16.to_le_bytes())
            .expect("table");
        let mut bytes = good.clone();
        bytes[table + 2..table + 4].copy_from_slice(&4u16.to_le_bytes());
        bytes[table + 4..table + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[table + 4..table + 8].fill(0);
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[table + 8..table + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Incomplete)));
        bytes = good;
        let last = u32::from_le_bytes(bytes[table + 8..table + 12].try_into().expect("pointer"))
            as usize
            + 2 * usize::from(u8::MAX);
        bytes[last..last + 2].copy_from_slice(&256u16.to_le_bytes());
        let plan = Plan::parse(&bytes).expect("valid Stage1").expect("RGB8");
        let image = Image::new(bytes, plan);
        assert!(!image.supports_final_render());
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut row = [0xa5a5; 9];
        assert_eq!(image.stage2_row(0, &mut row), Err(Error::Unsupported));
        assert_eq!(row, [0xa5a5; 9]);
        let mut rgb = [0xa5; 9];
        assert_eq!(image.copy_rgb_row(0, &mut rgb), Err(Error::Unsupported));
        assert_eq!(rgb, [0xa5; 9]);
    }

    fn field(bytes: &[u8], id: u16) -> usize {
        let count = u16::from_le_bytes(bytes[8..10].try_into().expect("count"));
        (0..count as usize)
            .map(|i| 10 + 12 * i)
            .find(|&at| u16::from_le_bytes(bytes[at..at + 2].try_into().expect("tag")) == id)
            .expect("tag")
    }

    #[test]
    fn reads_all_rgb_samples_and_expands_only_checked_identity_stages() {
        for big_endian in [false, true] {
            for multi in [false, true] {
                let bytes = fixture(big_endian, multi);
                let plan = Plan::parse(&bytes)
                    .expect("valid RGB8")
                    .expect("root image");
                let image = Image::new(bytes, plan);
                assert_eq!((image.width(), image.height()), (3, 3));
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Ok(()));
                for row in 0..3 {
                    let mut raw = [0xa5u8; 9];
                    let mut stage2 = [0xa5a5u16; 9];
                    let mut stage3 = [0xa5a5u16; 9];
                    image.raw_row(row, &mut raw).expect("raw row");
                    image.stage2_row(row, &mut stage2).expect("Stage 2");
                    image.stage3_row(row, &mut stage3).expect("Stage 3");
                    for i in 0..9 {
                        let value = PIXELS[row as usize * 9 + i];
                        assert_eq!(raw[i], value);
                        assert_eq!(stage2[i], u16::from(value) * 257);
                        assert_eq!(stage3[i], stage2[i]);
                    }
                    image.raw_row(row, &mut raw).expect("repeat row");
                    assert_eq!(raw, PIXELS[row as usize * 9..row as usize * 9 + 9]);
                }
                let mut wrong = [0xa5a5u16; 8];
                assert_eq!(image.stage2_row(0, &mut wrong), Err(Error::Invalid));
                assert_eq!(wrong, [0xa5a5; 8]);
                assert_eq!(image.raw_row(3, &mut [0u8; 9]), Err(Error::Invalid));
            }
        }
    }

    #[test]
    fn exact_srgb_profile_renders_color_in_both_endian_and_strip_layouts() {
        assert!(RGB8_FINAL_TAGS.windows(2).all(|pair| pair[0] < pair[1]));
        for big_endian in [false, true] {
            for multi in [false, true] {
                let bytes = fixture_with_srgb(big_endian, multi, true);
                let image = Image::new(bytes.clone(), Plan::parse(&bytes).unwrap().unwrap());
                assert!(image.supports_final_render());
                for row in 0..3 {
                    let mut rgb = [0xa5; 11];
                    image
                        .copy_rgb_row(row, &mut rgb[..9])
                        .expect("final RGB row");
                    for (actual, source) in rgb[..9]
                        .iter()
                        .zip(&PIXELS[row as usize * 9..row as usize * 9 + 9])
                    {
                        assert_eq!(*actual, srgb_from_linear_u8(*source));
                    }
                    assert_eq!(rgb[9..], [0xa5; 2]);
                }
                let mut untouched = [0xa5; 9];
                assert_eq!(image.copy_rgb_row(3, &mut untouched), Err(Error::Invalid));
                assert_eq!(
                    image.copy_rgb_row(0, &mut untouched[..8]),
                    Err(Error::Invalid)
                );
                assert_eq!(untouched, [0xa5; 9]);
            }
        }
    }

    #[test]
    fn color_matrix_forward_tone_and_neutral_changes_disable_final_output() {
        let good = fixture_with_srgb(false, false, true);
        for (tag, component, value) in [
            (50721, 0usize, 1038u32),
            (50964, 0, 28579),
            (50728, 8, 2),
            (50940, 8, 0x3f000000),
        ] {
            let mut changed = good.clone();
            let at = field(&changed, tag);
            let pointer = u32::from_le_bytes(changed[at + 8..at + 12].try_into().unwrap()) as usize;
            changed[pointer + component..pointer + component + 4]
                .copy_from_slice(&value.to_le_bytes());
            let image = Image::new(changed.clone(), Plan::parse(&changed).unwrap().unwrap());
            assert_eq!(image.stage3_status(), Ok(()));
            assert!(!image.supports_final_render(), "changed tag {tag}");
            let mut untouched = [0xa5; 9];
            assert_eq!(
                image.copy_rgb_row(0, &mut untouched),
                Err(Error::Unsupported)
            );
            assert_eq!(untouched, [0xa5; 9]);
        }
    }

    #[test]
    fn malformed_strips_and_processing_changes_do_not_publish_rows() {
        let bytes = fixture(false, true);
        assert!(matches!(
            Plan::parse(&bytes[..bytes.len() - 1]),
            Err(Error::Incomplete)
        ));
        let offsets_tag = field(&bytes, 273);
        let offsets = u32::from_le_bytes(
            bytes[offsets_tag + 8..offsets_tag + 12]
                .try_into()
                .expect("strip offsets"),
        ) as usize;
        let mut missing = bytes.clone();
        missing[offsets + 4..offsets + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Plan::parse(&missing), Err(Error::Incomplete)));
        let mut count = bytes.clone();
        count[offsets_tag + 4..offsets_tag + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Plan::parse(&count), Err(Error::Invalid)));
        let mut type_error = bytes.clone();
        let compression = field(&type_error, 259);
        type_error[compression + 2..compression + 4].copy_from_slice(&4u16.to_le_bytes());
        assert!(matches!(Plan::parse(&type_error), Ok(None)));
        assert!(matches!(
            crate::dng::Image::parse(type_error),
            Err(Error::Invalid)
        ));

        for (id, stage2_ok) in [(51009u16, false), (51022, true), (50712, false)] {
            let mut changed = bytes.clone();
            let profile = field(&changed, 50940);
            changed[profile..profile + 2].copy_from_slice(&id.to_le_bytes());
            changed[profile + 2..profile + 4]
                .copy_from_slice(&(if id == 50712 { 3u16 } else { 7u16 }).to_le_bytes());
            changed[profile + 4..profile + 8]
                .copy_from_slice(&(if id == 50712 { 2u32 } else { 4u32 }).to_le_bytes());
            if id == 50712 {
                changed[profile + 8..profile + 12].copy_from_slice(&[0, 0, 1, 0]);
            }
            let plan = Plan::parse(&changed).expect("valid Stage 1").expect("RGB8");
            let image = Image::new(changed, plan);
            assert_eq!(
                image.stage2_status(),
                if stage2_ok {
                    Ok(())
                } else {
                    Err(Error::Unsupported)
                }
            );
            assert_eq!(image.stage3_status(), Err(Error::Unsupported));
            let mut raw = [0u8; 9];
            image.raw_row(0, &mut raw).expect("Stage 1 unchanged");
            assert_eq!(raw, PIXELS[..9]);
            let mut output = [0xa5a5u16; 9];
            if stage2_ok {
                image.stage2_row(0, &mut output).expect("Stage 2 identity");
                output.fill(0xa5a5);
            } else {
                assert_eq!(image.stage2_row(0, &mut output), Err(Error::Unsupported));
                assert_eq!(output, [0xa5a5; 9]);
            }
            assert_eq!(image.stage3_row(0, &mut output), Err(Error::Unsupported));
            assert_eq!(output, [0xa5a5; 9]);
        }
        let mut crop = bytes.clone();
        let origin = field(&crop, 50719);
        let at =
            u32::from_le_bytes(crop[origin + 8..origin + 12].try_into().expect("crop")) as usize;
        crop[at..at + 4].copy_from_slice(&1u32.to_le_bytes());
        let plan = Plan::parse(&crop).expect("valid Stage 1").expect("RGB8");
        assert_eq!(
            Image::new(crop, plan).stage2_status(),
            Err(Error::Unsupported)
        );

        let mut subifd = bytes.clone();
        let profile = field(&subifd, 50940);
        subifd[profile..profile + 2].copy_from_slice(&330u16.to_le_bytes());
        assert!(matches!(Plan::parse(&subifd), Ok(None))); // Another reader must validate graph.
    }
}
