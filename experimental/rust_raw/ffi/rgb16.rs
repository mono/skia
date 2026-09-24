// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Narrow root-IFD, uncompressed 16-bit three-channel LinearRaw Stage 1.
//! Stage 2/3 are checked identity or linearized rows only when all relevant
//! processing metadata is validated. Profiles never authorize final rendering.

use super::deflate::reverse_horizontal_prediction;
use super::dng::{
    identity_array, integral, number, optional, range, required, scalar, ByteOrder, Error, Tag,
};
use super::linearization::LinearizationTable;
use super::tiled::read_ifd;

struct Strip {
    offset: usize,
    size: usize,
}

pub struct Plan {
    order: ByteOrder,
    width: u32,
    height: u32,
    main_ifd_index: u32,
    compression: u16,
    predictor: u16,
    rows_per_strip: u32,
    strips: Vec<Strip>,
    linearization: Option<LinearizationTable>,
    stage2_supported: bool,
    stage3_supported: bool,
}

pub struct Image {
    bytes: Vec<u8>,
    plan: Plan,
    pixels: Option<Vec<u8>>,
}

fn recognized_tag(tag: &Tag<'_>) -> Result<(), Error> {
    let kind = tag.kind;
    let valid = match tag.id {
        254 | 34665 | 50941 | 51110 => kind == 4,
        256 | 257 | 273 | 278 | 279 | 50717 | 51090 => kind == 3 || kind == 4,
        258 | 259 | 262 | 274 | 277 | 284 | 317 | 339 | 50712 | 50713 | 50778 | 33421 => kind == 3,
        305 | 306 | 50708 | 50936 => kind == 2,
        50706 | 50707 | 50781 | 51111 => kind == 1,
        50718 | 50727 | 50728 | 50731 | 50732 | 50734 | 50738 | 50739 | 50780 | 51091 => kind == 5,
        50714 | 50719 | 50720 => matches!(kind, 3 | 4 | 5),
        50721 | 50730 | 50715 | 50716 | 50964 => kind == 10,
        50940 => kind == 11,
        700 => kind == 1 || kind == 7,
        33422 => kind == 1,
        52525 | 52544 | 52548 | 52550 | 51009 | 51022 => kind == 7,
        50829 => kind == 3 || kind == 4,
        _ => return Err(Error::Unsupported),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

fn recognized_parent_tag(tag: &Tag<'_>) -> Result<(), Error> {
    let valid = match tag.id {
        330 | 50933 | 50970 => tag.kind == 4,
        529 | 532 => tag.kind == 5,
        530 | 531 => tag.kind == 3,
        50966 | 50967 | 50971 => tag.kind == 2,
        50969 => tag.kind == 1,
        _ => return recognized_tag(tag),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

struct Selection<'a> {
    raw: super::tiled::Ifd<'a>,
    parent: Option<super::tiled::Ifd<'a>>,
    index: u32,
    parent_stage2: bool,
    parent_stage3: bool,
}

fn select_subifd<'a>(
    data: &'a [u8],
    first: usize,
    order: ByteOrder,
) -> Result<Option<Selection<'a>>, Error> {
    let parent = read_ifd(data, first, order)?;
    if parent.next != 0 {
        return Err(if parent.next as usize == first {
            Error::Invalid
        } else {
            Error::Unsupported
        });
    }
    for tag in &parent.tags {
        recognized_parent_tag(tag)?;
    }
    let version = required(&parent.tags, 50706)?;
    if version.kind != 1 || version.count != 4 {
        return Err(Error::Invalid);
    }
    if version.value != &[1, 7, 0, 0] {
        return Err(Error::Unsupported);
    }
    let model = required(&parent.tags, 50708)?;
    if model.count < 2 || model.value.last() != Some(&0) {
        return Err(Error::Invalid);
    }
    let subifds = required(&parent.tags, 330)?;
    if subifds.kind != 4 || subifds.count == 0 {
        return Err(Error::Invalid);
    }
    if subifds.count > 125 {
        return Err(Error::Unsupported);
    }
    let preview_width = scalar(required(&parent.tags, 256)?, order)?;
    let preview_height = scalar(required(&parent.tags, 257)?, order)?;
    if preview_width == 0
        || preview_height == 0
        || preview_width > 300_000
        || preview_height > 300_000
    {
        return Err(Error::Invalid);
    }
    for (id, expected) in [(254, 1), (259, 7), (262, 6), (277, 3), (284, 1)] {
        if scalar(required(&parent.tags, id)?, order)? != expected {
            return Err(Error::Unsupported);
        }
    }
    let preview_bits = required(&parent.tags, 258)?;
    if preview_bits.count != 3 || (0..3).any(|i| number(preview_bits, i, order) != Ok(8)) {
        return Err(Error::Unsupported);
    }
    let orientation = scalar(required(&parent.tags, 274)?, order)?;
    if !(1..=8).contains(&orientation) {
        return Err(Error::Invalid);
    }
    if scalar(required(&parent.tags, 278)?, order)? == 0 {
        return Err(Error::Invalid);
    }
    for (id, count) in [(529, 3), (530, 2), (531, 1), (532, 6)] {
        if optional(&parent.tags, id).is_some_and(|tag| tag.count != count) {
            return Err(Error::Invalid);
        }
    }
    // The preview's image data is not decoded here, but its declared range
    // must still be a valid part of this classic-TIFF container.
    let preview_offset = scalar(required(&parent.tags, 273)?, order)? as usize;
    let preview_size = scalar(required(&parent.tags, 279)?, order)? as usize;
    if preview_size == 0 {
        return Err(Error::Invalid);
    }
    range(data, preview_offset, preview_size)?;
    if let Some(profiles) = optional(&parent.tags, 50933) {
        if profiles.count != 1 {
            return Err(Error::Unsupported);
        }
        range(data, scalar(profiles, order)? as usize, 2)?;
    }
    let parent_stage2 = !parent.tags.iter().any(|tag| {
        matches!(
            tag.id,
            33421
                | 33422
                | 50712
                | 50713
                | 50714
                | 50715
                | 50716
                | 50718
                | 50719
                | 50720
                | 50829
                | 50879
                | 51009
        )
    }) && optional(&parent.tags, 50734)
        .is_none_or(|tag| tag.count == 1 && integral(tag, 0, order) == Ok(1));
    let parent_stage3 = parent_stage2 && optional(&parent.tags, 51022).is_none();

    let mut seen = Vec::new();
    seen.try_reserve_exact(subifds.count as usize)
        .map_err(|_| Error::OutOfMemory)?;
    let mut selected = None;
    for i in 0..subifds.count as usize {
        let offset = number(subifds, i, order)? as usize;
        if offset == 0 || offset == first || seen.contains(&offset) {
            return Err(Error::Invalid);
        }
        seen.push(offset);
        let child = read_ifd(data, offset, order)?;
        if child.next != 0 || optional(&child.tags, 330).is_some() {
            return Err(Error::Unsupported);
        }
        let subfile = optional(&child.tags, 254)
            .ok_or(Error::Invalid)
            .and_then(|tag| {
                if tag.kind == 4 {
                    scalar(tag, order)
                } else {
                    Err(Error::Invalid)
                }
            })?;
        if subfile != 0 {
            continue;
        }
        let photo = required(&child.tags, 262)?;
        if photo.kind != 3 {
            return Err(Error::Invalid);
        }
        if scalar(photo, order)? != 34892 {
            return Err(Error::Unsupported);
        }
        if selected.replace((child, (i + 1) as u32)).is_some() {
            return Err(Error::Unsupported);
        }
    }
    let (raw, index) = selected.ok_or(Error::Unsupported)?;
    Ok(Some(Selection {
        raw,
        parent: Some(parent),
        index,
        parent_stage2,
        parent_stage3,
    }))
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
        let has_subifd = entries
            .chunks_exact(12)
            .any(|entry| order.u16(&entry[..2]) == 330);
        let is_rgb = entries.chunks_exact(12).any(|entry| {
            order.u16(&entry[..2]) == 277
                && order.u16(&entry[2..4]) == 3
                && order.u32(&entry[4..8]) == 1
                && order.u16(&entry[8..10]) == 3
        });
        if !has_subifd && !is_rgb {
            return Ok(None); // Preserve the strict monochrome parser's errors.
        }
        if !has_subifd {
            let rgb16_bits = entries.chunks_exact(12).find(|entry| {
                order.u16(&entry[..2]) == 258
                    && order.u16(&entry[2..4]) == 3
                    && order.u32(&entry[4..8]) == 3
            });
            let Some(bits) = rgb16_bits else {
                return Ok(None);
            };
            let samples = range(data, order.u32(&bits[8..12]) as usize, 6)?;
            if (0..3).any(|i| order.u16(&samples[i * 2..i * 2 + 2]) != 16) {
                return Ok(None); // RGB8/JPEG is handled by the tiled route.
            }
            let encoded = entries
                .chunks_exact(12)
                .find(|entry| order.u16(&entry[..2]) == 259);
            if encoded.is_some_and(|entry| {
                order.u16(&entry[2..4]) == 3
                    && order.u32(&entry[4..8]) == 1
                    && matches!(order.u16(&entry[8..10]), 1 | 8)
                    && order.u16(&entry[8..10]) != compression
            }) {
                return Ok(None);
            }
        }
        let selection = if has_subifd {
            // DNG 1.4 SubIFD JPEG belongs to the existing tiled reader.
            let is_new_dng = entries.chunks_exact(12).any(|entry| {
                order.u16(&entry[..2]) == 50706
                    && order.u16(&entry[2..4]) == 1
                    && order.u32(&entry[4..8]) == 4
                    && entry[8..12] == [1, 7, 0, 0]
            });
            if !is_new_dng {
                return Ok(None);
            }
            select_subifd(data, first, order)?.ok_or(Error::Unsupported)?
        } else {
            let raw = read_ifd(data, first, order)?;
            if raw.next != 0 {
                return Err(Error::Unsupported);
            }
            Selection {
                raw,
                parent: None,
                index: 0,
                parent_stage2: true,
                parent_stage3: true,
            }
        };
        let tags = &selection.raw.tags;
        if selection.parent.is_some()
            && [50706, 50707, 50708]
                .into_iter()
                .any(|id| optional(tags, id).is_some())
        {
            return Err(Error::Unsupported);
        }
        for tag in tags {
            recognized_tag(tag)?;
        }
        let selected_compression = scalar(required(tags, 259)?, order)?;
        if selected_compression != u32::from(compression) && matches!(selected_compression, 1 | 8) {
            return Ok(None);
        }
        let metadata = selection
            .parent
            .as_ref()
            .map_or(tags.as_slice(), |ifd| ifd.tags.as_slice());
        let version = required(metadata, 50706)?;
        let backward = required(metadata, 50707)?;
        if version.kind != 1 || version.count != 4 || backward.kind != 1 || backward.count != 4 {
            return Err(Error::Invalid);
        }
        if (version.value != &[1, 6, 0, 0] && version.value != &[1, 7, 0, 0])
            || backward.value != &[1, 1, 0, 0]
        {
            return Err(Error::Unsupported);
        }
        let model = required(metadata, 50708)?;
        if model.count < 2 || model.value.last() != Some(&0) {
            return Err(Error::Invalid);
        }
        if let Some(black_render) = optional(metadata, 51110) {
            if black_render.count != 1 || scalar(black_render, order)? > 1 {
                return Err(Error::Invalid);
            }
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
        if !(predictor == 1 || compression == 8 && predictor == 2) {
            return Err(Error::Unsupported);
        }
        if let Some(sample_format) = optional(tags, 339) {
            if scalar(sample_format, order)? != 1 {
                return Err(Error::Unsupported);
            }
        }
        let bits = required(tags, 258)?;
        if bits.count != 3 || (0..3).any(|i| number(bits, i, order) != Ok(16)) {
            return Err(Error::Unsupported);
        }
        let linearization = optional(tags, 50712)
            .map(|tag| LinearizationTable::parse(tag, order))
            .transpose()?;
        let white_identity = optional(tags, 50717).is_some_and(|white| {
            white.count == 3 && (0..3).all(|i| number(white, i, order) == Ok(65535))
        });
        let black_repeat_identity = optional(tags, 50713)
            .is_none_or(|_| identity_array(tags, 50713, &[1, 1], order).is_ok());
        let black_identity = optional(tags, 50714).is_none_or(|black| {
            black.count == 3 && (0..3).all(|i| integral(black, i, order) == Ok(0))
        });
        let single_factor_identity = [50734, 50738, 50780].into_iter().all(|id| {
            optional(tags, id).is_none_or(|tag| tag.count == 1 && integral(tag, 0, order) == Ok(1))
        });
        let geometry_identity = optional(tags, 274).is_none_or(|tag| scalar(tag, order) == Ok(1))
            && optional(tags, 50718)
                .is_some_and(|_| identity_array(tags, 50718, &[1, 1], order).is_ok())
            && optional(tags, 50719)
                .is_some_and(|_| identity_array(tags, 50719, &[0, 0], order).is_ok())
            && optional(tags, 50720)
                .is_some_and(|_| identity_array(tags, 50720, &[width, height], order).is_ok())
            && identity_array(tags, 50829, &[0, 0, height, width], order).is_ok();
        let stage2_supported = selection.parent_stage2
            && white_identity
            && black_repeat_identity
            && black_identity
            && single_factor_identity
            && geometry_identity
            && linearization
                .as_ref()
                .is_none_or(LinearizationTable::has_identity_endpoints)
            && [50715, 50716, 33421, 33422, 51009]
                .into_iter()
                .all(|id| optional(tags, id).is_none());
        let stage3_supported =
            stage2_supported && selection.parent_stage3 && optional(tags, 51022).is_none();

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
            .and_then(|count| count.checked_mul(2))
            .ok_or(Error::Invalid)?;
        for index in 0..expected_count {
            let first_row = index.checked_mul(rows_per_strip).ok_or(Error::Invalid)?;
            let rows = (height - first_row).min(rows_per_strip);
            let expected_size = (rows as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            let size = number(lengths, index as usize, order)? as usize;
            if size == 0 || compression == 1 && size != expected_size {
                return Err(Error::Invalid);
            }
            let offset = number(offsets, index as usize, order)? as usize;
            range(data, offset, size)?;
        }
        let mut strips = Vec::new();
        strips
            .try_reserve_exact(expected_count as usize)
            .map_err(|_| Error::OutOfMemory)?;
        for index in 0..expected_count {
            let offset = number(offsets, index as usize, order)? as usize;
            let size = number(lengths, index as usize, order)? as usize;
            strips.push(Strip { offset, size });
        }
        Ok(Some(Self {
            order,
            width,
            height,
            main_ifd_index: selection.index,
            compression,
            predictor: predictor as u16,
            rows_per_strip,
            strips,
            linearization,
            stage2_supported,
            stage3_supported,
        }))
    }
}

impl Image {
    pub fn new(bytes: Vec<u8>, plan: Plan) -> Self {
        Self {
            bytes,
            plan,
            pixels: None,
        }
    }

    pub fn inflate(
        bytes: Vec<u8>,
        plan: Plan,
        mut validate: impl FnMut(&[u8], usize) -> Result<(), Error>,
        mut decompress: impl FnMut(&[u8], &mut [u8]) -> Result<(), Error>,
    ) -> Result<Self, Error> {
        if plan.compression != 8 {
            return Err(Error::Unsupported);
        }
        let row_bytes = (plan.width as usize).checked_mul(6).ok_or(Error::Invalid)?;
        let total_bytes = (plan.height as usize)
            .checked_mul(row_bytes)
            .ok_or(Error::Invalid)?;
        for (index, strip) in plan.strips.iter().enumerate() {
            let start_row = index
                .checked_mul(plan.rows_per_strip as usize)
                .ok_or(Error::Invalid)?;
            let remaining = (plan.height as usize)
                .checked_sub(start_row)
                .ok_or(Error::Invalid)?;
            let rows = remaining.min(plan.rows_per_strip as usize);
            let size = rows.checked_mul(row_bytes).ok_or(Error::Invalid)?;
            validate(range(&bytes, strip.offset, strip.size)?, size)?;
        }
        let mut pixels = Vec::new();
        pixels
            .try_reserve_exact(total_bytes)
            .map_err(|_| Error::OutOfMemory)?;
        pixels.resize(total_bytes, 0);
        for (index, strip) in plan.strips.iter().enumerate() {
            let start_row = index
                .checked_mul(plan.rows_per_strip as usize)
                .ok_or(Error::Invalid)?;
            let remaining = (plan.height as usize)
                .checked_sub(start_row)
                .ok_or(Error::Invalid)?;
            let rows = remaining.min(plan.rows_per_strip as usize);
            let size = rows.checked_mul(row_bytes).ok_or(Error::Invalid)?;
            let first = start_row.checked_mul(row_bytes).ok_or(Error::Invalid)?;
            let dest = pixels
                .get_mut(first..first.checked_add(size).ok_or(Error::Invalid)?)
                .ok_or(Error::Invalid)?;
            decompress(range(&bytes, strip.offset, strip.size)?, dest)?;
            if plan.predictor == 2 {
                reverse_horizontal_prediction(dest, row_bytes, plan.order, 3)?;
            }
        }
        Ok(Self {
            bytes: Vec::new(),
            plan,
            pixels: Some(pixels),
        })
    }

    pub fn width(&self) -> u32 {
        self.plan.width
    }

    pub fn height(&self) -> u32 {
        self.plan.height
    }

    pub fn main_ifd_index(&self) -> u32 {
        self.plan.main_ifd_index
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

    pub fn stage2_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage2_status()?;
        self.row(row, output)?;
        if let Some(table) = &self.plan.linearization {
            for sample in output {
                *sample = table.map(*sample);
            }
        }
        Ok(())
    }

    pub fn stage3_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage3_status()?;
        self.stage2_row(row, output)
    }

    pub fn row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        let expected = (self.plan.width as usize)
            .checked_mul(3)
            .ok_or(Error::Invalid)?;
        if row >= self.plan.height || output.len() != expected {
            return Err(Error::Invalid);
        }
        let row_bytes = expected.checked_mul(2).ok_or(Error::Invalid)?;
        let bytes = if let Some(pixels) = &self.pixels {
            let offset = (row as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            range(pixels, offset, row_bytes)?
        } else {
            let strip = &self.plan.strips[(row / self.plan.rows_per_strip) as usize];
            let offset = ((row % self.plan.rows_per_strip) as usize)
                .checked_mul(row_bytes)
                .and_then(|bytes| strip.offset.checked_add(bytes))
                .ok_or(Error::Invalid)?;
            range(&self.bytes, offset, row_bytes)?
        };
        for (source, dest) in bytes.chunks_exact(2).zip(output.iter_mut()) {
            *dest = self.plan.order.u16(source);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{Error, Image, Plan};
    use std::cell::Cell;

    const SAMPLES: [u16; 27] = [
        0, 1, 65535, 256, 257, 258, 1000, 2000, 3000, 4000, 5000, 6000, 0, 65535, 32768, 12345,
        23456, 34567, 7, 8, 9, 50000, 60000, 65534, 42, 43, 44,
    ];

    fn fixture(big_endian: bool, version: u8) -> Vec<u8> {
        fixture_with_metadata(big_endian, version, true)
    }

    fn fixture_with_metadata(big_endian: bool, version: u8, global_metadata: bool) -> Vec<u8> {
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
        let rational_pair = |a: u32, b: u32| {
            let mut value = Vec::new();
            for n in [a, 1, b, 1] {
                value.extend_from_slice(&long(n));
            }
            value
        };
        let mut fields = vec![
            (254, 4, 1, long(0).to_vec()),
            (256, 4, 1, long(3).to_vec()),
            (257, 4, 1, long(3).to_vec()),
            (258, 3, 3, [word(16); 3].concat()),
            (259, 3, 1, word(1).to_vec()),
            (262, 3, 1, word(34892).to_vec()),
            (273, 4, 2, vec![0; 8]),
            (274, 3, 1, word(1).to_vec()),
            (277, 3, 1, word(3).to_vec()),
            (278, 4, 1, long(2).to_vec()),
            (279, 4, 2, [long(36), long(18)].concat()),
            (284, 3, 1, word(1).to_vec()),
            (339, 3, 1, word(1).to_vec()),
            (50706, 1, 4, vec![1, version, 0, 0]),
            (50707, 1, 4, vec![1, 1, 0, 0]),
            (50708, 2, 5, b"test\0".to_vec()),
            (50717, 3, 3, [word(65535); 3].concat()),
            (50718, 5, 2, rational_pair(1, 1)),
            (50719, 5, 2, rational_pair(0, 0)),
            (50720, 5, 2, rational_pair(3, 3)),
            (50738, 5, 1, [long(1), long(1)].concat()),
            (50780, 5, 1, [long(1), long(1)].concat()),
            (52548, 7, 4, vec![0; 4]),
        ];
        if !global_metadata {
            fields.retain(|field| !matches!(field.0, 50706 | 50707 | 50708));
        }
        fields.sort_unstable_by_key(|field| field.0);
        let mut bytes = if big_endian {
            b"MM\0\x2a\0\0\0\x08".to_vec()
        } else {
            b"II\x2a\0\x08\0\0\0".to_vec()
        };
        bytes.extend_from_slice(&word(fields.len() as u16));
        let entries_start = bytes.len();
        bytes.resize(entries_start + fields.len() * 12 + 4, 0);
        let mut offsets = 0usize;
        for (index, (id, kind, count, value)) in fields.iter().enumerate() {
            let at = entries_start + index * 12;
            bytes[at..at + 2].copy_from_slice(&word(*id));
            bytes[at + 2..at + 4].copy_from_slice(&word(*kind));
            bytes[at + 4..at + 8].copy_from_slice(&long(*count));
            if value.len() <= 4 {
                bytes[at + 8..at + 8 + value.len()].copy_from_slice(value);
            } else {
                if bytes.len() % 2 != 0 {
                    bytes.push(0);
                }
                let offset = bytes.len();
                bytes[at + 8..at + 12].copy_from_slice(&long(offset as u32));
                bytes.extend_from_slice(value);
                if *id == 273 {
                    offsets = offset;
                }
            }
        }
        assert!(offsets != 0);
        for (strip, sample_range) in [(0usize, 0..18), (1, 18..27)] {
            if bytes.len() % 2 != 0 {
                bytes.push(0);
            }
            let offset = bytes.len() as u32;
            bytes[offsets + strip * 4..offsets + strip * 4 + 4].copy_from_slice(&long(offset));
            for &sample in &SAMPLES[sample_range] {
                bytes.extend_from_slice(&word(sample));
            }
        }
        bytes
    }

    fn wrap_preview(mut child: Vec<u8>, big_endian: bool) -> Vec<u8> {
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
        if child.len() % 2 != 0 {
            child.push(0);
        }
        let preview = child.len() as u32;
        child.extend_from_slice(&[0xff, 0xd8, 0xff, 0xd9]);
        let root = child.len() as u32;
        let mut fields = vec![
            (254, 4, 1, long(1).to_vec()),
            (256, 4, 1, long(2).to_vec()),
            (257, 4, 1, long(2).to_vec()),
            (258, 3, 3, [word(8); 3].concat()),
            (259, 3, 1, word(7).to_vec()),
            (262, 3, 1, word(6).to_vec()),
            (273, 4, 1, long(preview).to_vec()),
            (274, 3, 1, word(1).to_vec()),
            (277, 3, 1, word(3).to_vec()),
            (278, 4, 1, long(2).to_vec()),
            (279, 4, 1, long(4).to_vec()),
            (284, 3, 1, word(1).to_vec()),
            (330, 4, 1, long(8).to_vec()),
            (50706, 1, 4, vec![1, 7, 0, 0]),
            (50707, 1, 4, vec![1, 1, 0, 0]),
            (50708, 2, 5, b"test\0".to_vec()),
            (50734, 5, 1, [long(1), long(1)].concat()),
        ];
        fields.sort_unstable_by_key(|field| field.0);
        child[4..8].copy_from_slice(&long(root));
        child.extend_from_slice(&word(fields.len() as u16));
        let start = child.len();
        child.resize(start + fields.len() * 12 + 4, 0);
        for (i, (tag, kind, count, value)) in fields.into_iter().enumerate() {
            let at = start + 12 * i;
            child[at..at + 2].copy_from_slice(&word(tag));
            child[at + 2..at + 4].copy_from_slice(&word(kind));
            child[at + 4..at + 8].copy_from_slice(&long(count));
            if value.len() <= 4 {
                child[at + 8..at + 8 + value.len()].copy_from_slice(&value);
            } else {
                if child.len() % 2 != 0 {
                    child.push(0);
                }
                let offset = child.len() as u32;
                child[at + 8..at + 12].copy_from_slice(&long(offset));
                child.extend_from_slice(&value);
            }
        }
        child
    }

    fn field(bytes: &[u8], tag: u16) -> usize {
        let count = u16::from_le_bytes(bytes[8..10].try_into().expect("IFD count"));
        (0..count as usize)
            .map(|i| 10 + i * 12)
            .find(|&at| u16::from_le_bytes(bytes[at..at + 2].try_into().expect("tag")) == tag)
            .expect("required test tag")
    }

    fn root_field(bytes: &[u8], tag: u16) -> usize {
        let root = u32::from_le_bytes(bytes[4..8].try_into().expect("root offset")) as usize;
        let count = u16::from_le_bytes(bytes[root..root + 2].try_into().expect("root count"));
        (0..count as usize)
            .map(|i| root + 2 + i * 12)
            .find(|&at| u16::from_le_bytes(bytes[at..at + 2].try_into().expect("tag")) == tag)
            .expect("required root tag")
    }

    fn deflate_fixture(big_endian: bool, predictor: u16) -> Vec<u8> {
        deflate_fixture_with_metadata(big_endian, predictor, true)
    }

    fn deflate_fixture_with_metadata(
        big_endian: bool,
        predictor: u16,
        global_metadata: bool,
    ) -> Vec<u8> {
        let mut bytes = fixture_with_metadata(big_endian, 7, global_metadata);
        let word = |value: u16| {
            if big_endian {
                value.to_be_bytes()
            } else {
                value.to_le_bytes()
            }
        };
        let long = |value: u32| {
            if big_endian {
                value.to_be_bytes()
            } else {
                value.to_le_bytes()
            }
        };
        let find = |data: &[u8], tag: u16| {
            let count = if big_endian {
                u16::from_be_bytes(data[8..10].try_into().expect("IFD count"))
            } else {
                u16::from_le_bytes(data[8..10].try_into().expect("IFD count"))
            };
            (0..count as usize)
                .map(|i| 10 + i * 12)
                .find(|&at| data[at..at + 2] == word(tag))
                .expect("required test field")
        };
        let compression = find(&bytes, 259);
        bytes[compression + 8..compression + 10].copy_from_slice(&word(8));
        let marker = find(&bytes, 52548);
        bytes[marker..marker + 2].copy_from_slice(&word(317));
        bytes[marker + 2..marker + 4].copy_from_slice(&word(3));
        bytes[marker + 4..marker + 8].copy_from_slice(&long(1));
        bytes[marker + 8..marker + 10].copy_from_slice(&word(predictor));
        bytes[marker + 10..marker + 12].fill(0);
        bytes
    }

    fn linearized_fixture(big_endian: bool, compressed: bool, short: bool) -> Vec<u8> {
        let mut bytes = if compressed {
            deflate_fixture(big_endian, 2)
        } else {
            fixture(big_endian, 7)
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
            .map(|entry| entry.try_into().expect("tag entry"))
            .collect();
        let value = if short {
            [word(0), word(u16::MAX)].concat()
        } else {
            if bytes.len() & 1 != 0 {
                bytes.push(0);
            }
            let start = bytes.len();
            for sample in 0..=u16::MAX {
                bytes.extend_from_slice(&word(sample.saturating_mul(2)));
            }
            long(start as u32).to_vec()
        };
        let tag: [u8; 12] = [
            word(50712).as_slice(),
            word(3).as_slice(),
            long(if short { 2 } else { 65536 }).as_slice(),
            value.as_slice(),
        ]
        .concat()
        .try_into()
        .expect("new tag");
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

    fn copy_predicted_rgb16(encoded: &[u8], output: &mut [u8], big_endian: bool, predictor: u16) {
        assert_eq!(encoded.len(), output.len());
        for (source, dest) in encoded.chunks_exact(18).zip(output.chunks_exact_mut(18)) {
            for lane in 0..9 {
                let sample_at = |lane| {
                    let bytes = [source[2 * lane], source[2 * lane + 1]];
                    if big_endian {
                        u16::from_be_bytes(bytes)
                    } else {
                        u16::from_le_bytes(bytes)
                    }
                };
                let previous = if predictor == 2 && lane >= 3 {
                    sample_at(lane - 3)
                } else {
                    0
                };
                let difference = sample_at(lane).wrapping_sub(previous);
                dest[2 * lane..2 * lane + 2].copy_from_slice(&if big_endian {
                    difference.to_be_bytes()
                } else {
                    difference.to_le_bytes()
                });
            }
        }
    }

    #[test]
    fn deflate_rgb16_validates_all_strips_then_decodes_predictor1_and_2() {
        for big_endian in [false, true] {
            for predictor in [1, 2] {
                let bytes = deflate_fixture(big_endian, predictor);
                assert!(matches!(Plan::parse(&bytes), Ok(None)));
                let plan = Plan::parse_deflate(&bytes)
                    .expect("checked Deflate metadata")
                    .expect("RGB16 route");
                let validated = Cell::new(0);
                let inflated = Cell::new(0);
                let image = Image::inflate(
                    bytes,
                    plan,
                    |encoded, expected| {
                        assert_eq!(encoded.len(), expected);
                        validated.set(validated.get() + 1);
                        Ok(())
                    },
                    |encoded, output| {
                        copy_predicted_rgb16(encoded, output, big_endian, predictor);
                        inflated.set(inflated.get() + 1);
                        Ok(())
                    },
                )
                .expect("preflighted RGB16 Deflate");
                assert_eq!((validated.get(), inflated.get()), (2, 2));
                assert!(image.bytes.is_empty());
                assert_eq!(image.pixels.as_ref().map(Vec::len), Some(54));
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Ok(()));
                for y in 0..3 {
                    let mut row = [0xa5a5; 10];
                    image.row(y, &mut row[..9]).expect("Stage 1");
                    assert_eq!(row[..9], SAMPLES[y as usize * 9..(y + 1) as usize * 9]);
                    image.stage2_row(y, &mut row[..9]).expect("Stage 2");
                    assert_eq!(row[..9], SAMPLES[y as usize * 9..(y + 1) as usize * 9]);
                    image.stage3_row(y, &mut row[..9]).expect("Stage 3");
                    assert_eq!(row[..9], SAMPLES[y as usize * 9..(y + 1) as usize * 9]);
                    assert_eq!(row[9], 0xa5a5);
                }
            }
        }
    }

    #[test]
    fn shared_linearization_maps_rgb16_channels_after_stage1() {
        for big_endian in [false, true] {
            for compressed in [false, true] {
                for short in [false, true] {
                    let bytes = linearized_fixture(big_endian, compressed, short);
                    let plan = if compressed {
                        Plan::parse_deflate(&bytes)
                    } else {
                        Plan::parse(&bytes)
                    }
                    .expect("checked table")
                    .expect("RGB16 root");
                    let image = if compressed {
                        Image::inflate(
                            bytes,
                            plan,
                            |encoded, expected| {
                                assert_eq!(encoded.len(), expected);
                                Ok(())
                            },
                            |encoded, output| {
                                copy_predicted_rgb16(encoded, output, big_endian, 2);
                                Ok(())
                            },
                        )
                        .expect("preflighted RGB16")
                    } else {
                        Image::new(bytes, plan)
                    };
                    assert_eq!(image.stage2_status(), Ok(()));
                    assert_eq!(image.stage3_status(), Ok(()));
                    for y in 0..3 {
                        let mut raw = [0xa5a5; 10];
                        let mut mapped = [0xa5a5; 10];
                        image.row(y, &mut raw[..9]).expect("unmapped raw row");
                        image.stage2_row(y, &mut mapped[..9]).expect("mapped row");
                        assert_eq!(raw[..9], SAMPLES[y as usize * 9..(y + 1) as usize * 9]);
                        for x in 0..9 {
                            assert_eq!(
                                mapped[x],
                                if short {
                                    if raw[x] == 0 {
                                        0
                                    } else {
                                        u16::MAX
                                    }
                                } else {
                                    raw[x].saturating_mul(2)
                                }
                            );
                        }
                        assert_eq!((raw[9], mapped[9]), (0xa5a5, 0xa5a5));
                        let expected = mapped;
                        mapped.fill(0xa5a5);
                        image
                            .stage3_row(y, &mut mapped[..9])
                            .expect("Stage3 identity after mapping");
                        assert_eq!(mapped[..9], expected[..9]);
                        assert_eq!(mapped[9], 0xa5a5);
                    }
                }
            }
        }
    }

    #[test]
    fn malformed_rgb16_linearization_preserves_raw_and_gates_stages() {
        let good = linearized_fixture(false, false, false);
        let ifd = u32::from_le_bytes(good[4..8].try_into().expect("IFD")) as usize;
        let count = u16::from_le_bytes(good[ifd..ifd + 2].try_into().expect("count")) as usize;
        let tag = (0..count)
            .map(|index| ifd + 2 + index * 12)
            .find(|&offset| good[offset..offset + 2] == 50712u16.to_le_bytes())
            .expect("linearization table");
        let mut bytes = good.clone();
        bytes[tag + 2..tag + 4].copy_from_slice(&4u16.to_le_bytes());
        bytes[tag + 4..tag + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[tag + 4..tag + 8].copy_from_slice(&0u32.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[tag + 8..tag + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Incomplete)));
        bytes = good;
        let last = u32::from_le_bytes(bytes[tag + 8..tag + 12].try_into().expect("table")) as usize
            + 2 * u16::MAX as usize;
        bytes[last..last + 2].fill(0);
        let plan = Plan::parse(&bytes).expect("valid Stage1").expect("RGB16");
        let image = Image::new(bytes, plan);
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut row = [0xa5a5; 9];
        image.row(0, &mut row).expect("raw bytes retained");
        assert_eq!(row, SAMPLES[..9]);
        row.fill(0xa5a5);
        assert_eq!(image.stage2_row(0, &mut row), Err(Error::Unsupported));
        assert_eq!(row, [0xa5a5; 9]);
    }

    #[test]
    fn deflate_rgb16_preview_subifd_keeps_checked_stage_rows() {
        for big_endian in [false, true] {
            for predictor in [1, 2] {
                let bytes = wrap_preview(
                    deflate_fixture_with_metadata(big_endian, predictor, false),
                    big_endian,
                );
                assert!(Plan::parse(&bytes).expect("uncompressed route").is_none());
                let plan = Plan::parse_deflate(&bytes)
                    .expect("checked parent and child")
                    .expect("RGB16 Deflate SubIFD");
                let image = Image::inflate(
                    bytes,
                    plan,
                    |encoded, expected| {
                        assert_eq!(encoded.len(), expected);
                        Ok(())
                    },
                    |encoded, output| {
                        copy_predicted_rgb16(encoded, output, big_endian, predictor);
                        Ok(())
                    },
                )
                .expect("preflighted child");
                assert_eq!(
                    (image.main_ifd_index(), image.width(), image.height()),
                    (1, 3, 3)
                );
                assert!(image.bytes.is_empty());
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Ok(()));
                for y in 0..3 {
                    let mut row = [0xa5a5; 10];
                    image.stage3_row(y, &mut row[..9]).expect("Stage 3");
                    assert_eq!(row[..9], SAMPLES[y as usize * 9..(y + 1) as usize * 9]);
                    assert_eq!(row[9], 0xa5a5);
                }
            }
        }
    }

    #[test]
    fn deflate_rgb16_rejects_malformed_offsets_and_failed_preflight() {
        let bytes = deflate_fixture(false, 2);
        let mut invalid = bytes.clone();
        let offsets = field(&invalid, 273);
        let values = u32::from_le_bytes(
            invalid[offsets + 8..offsets + 12]
                .try_into()
                .expect("offset pointer"),
        ) as usize;
        invalid[values + 4..values + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            Plan::parse_deflate(&invalid),
            Err(Error::Incomplete)
        ));

        let mut bad_predictor = bytes.clone();
        let field = field(&bad_predictor, 317);
        bad_predictor[field + 8..field + 10].copy_from_slice(&3u16.to_le_bytes());
        assert!(matches!(
            Plan::parse_deflate(&bad_predictor),
            Err(Error::Unsupported)
        ));

        let plan = Plan::parse_deflate(&bytes).unwrap().unwrap();
        let calls = Cell::new(0);
        let decoded = Cell::new(0);
        assert!(matches!(
            Image::inflate(
                bytes,
                plan,
                |_, _| {
                    calls.set(calls.get() + 1);
                    if calls.get() == 2 {
                        Err(Error::Incomplete)
                    } else {
                        Ok(())
                    }
                },
                |_, _| {
                    decoded.set(decoded.get() + 1);
                    Ok(())
                }
            ),
            Err(Error::Incomplete)
        ));
        assert_eq!((calls.get(), decoded.get()), (2, 0));
    }

    #[test]
    fn reads_every_sample_in_multistrip_endian_profiles() {
        for version in [6, 7] {
            for big_endian in [false, true] {
                let bytes = fixture(big_endian, version);
                let plan = Plan::parse(&bytes)
                    .expect("valid IFD")
                    .expect("RGB16 route");
                let image = Image::new(bytes, plan);
                assert_eq!(
                    (image.width(), image.height(), image.main_ifd_index()),
                    (3, 3, 0)
                );
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Ok(()));
                for row in 0..3 {
                    let mut output = [0u16; 9];
                    image.row(row, &mut output).expect("decoded row");
                    assert_eq!(output, SAMPLES[row as usize * 9..row as usize * 9 + 9]);
                    image
                        .stage2_row(row, &mut output)
                        .expect("identity stage 2");
                    assert_eq!(output, SAMPLES[row as usize * 9..row as usize * 9 + 9]);
                    image
                        .stage3_row(row, &mut output)
                        .expect("identity stage 3");
                    assert_eq!(output, SAMPLES[row as usize * 9..row as usize * 9 + 9]);
                    image.row(row, &mut output).expect("repeat decode");
                    assert_eq!(output, SAMPLES[row as usize * 9..row as usize * 9 + 9]);
                }
                let mut wrong = [0xa5a5; 8];
                assert_eq!(image.row(0, &mut wrong), Err(Error::Invalid));
                assert_eq!(wrong, [0xa5a5; 8]);
                assert_eq!(image.row(3, &mut [0u16; 9]), Err(Error::Invalid));
            }
        }
    }

    #[test]
    fn explicit_none_black_render_preserves_raw_and_identity_stages() {
        let mut bytes = fixture(false, 7);
        let at = field(&bytes, 52548);
        bytes[at..at + 2].copy_from_slice(&51110u16.to_le_bytes());
        bytes[at + 2..at + 4].copy_from_slice(&4u16.to_le_bytes());
        bytes[at + 4..at + 8].copy_from_slice(&1u32.to_le_bytes());
        bytes[at + 8..at + 12].copy_from_slice(&1u32.to_le_bytes());
        let image = Image::new(
            bytes.clone(),
            Plan::parse(&bytes)
                .expect("checked metadata")
                .expect("RGB16"),
        );
        assert_eq!(image.stage2_status(), Ok(()));
        assert_eq!(image.stage3_status(), Ok(()));
        let mut row = [0u16; 9];
        image.stage3_row(0, &mut row).expect("identity samples");
        assert_eq!(row, SAMPLES[..9]);

        bytes[at + 8..at + 12].copy_from_slice(&2u32.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
        bytes[at + 8..at + 12].copy_from_slice(&1u32.to_le_bytes());
        bytes[at + 2..at + 4].copy_from_slice(&7u16.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
    }

    #[test]
    fn preview_ifd_selects_checked_rgb16_subifd_without_changing_rows() {
        for big_endian in [false, true] {
            let bytes = wrap_preview(fixture_with_metadata(big_endian, 7, false), big_endian);
            let plan = Plan::parse(&bytes)
                .expect("valid graph")
                .expect("RGB16 SubIFD");
            let image = Image::new(bytes, plan);
            assert_eq!(
                (image.main_ifd_index(), image.width(), image.height()),
                (1, 3, 3)
            );
            assert_eq!(image.stage2_status(), Ok(()));
            assert_eq!(image.stage3_status(), Ok(()));
            for row in 0..3 {
                let mut raw = [0u16; 9];
                let mut second = [0u16; 9];
                image.row(row, &mut raw).expect("stage 1");
                image.stage2_row(row, &mut second).expect("stage 2");
                assert_eq!(raw, second);
                image.stage3_row(row, &mut second).expect("stage 3");
                assert_eq!(raw, second);
                assert_eq!(raw, SAMPLES[row as usize * 9..row as usize * 9 + 9]);
            }
        }
    }

    #[test]
    fn preview_graph_rejects_bad_pointers_and_gates_parent_processing() {
        let bytes = wrap_preview(fixture_with_metadata(false, 7, false), false);
        let sub = root_field(&bytes, 330);
        let root = u32::from_le_bytes(bytes[4..8].try_into().expect("root pointer"));

        let mut cycle = bytes.clone();
        cycle[sub + 8..sub + 12].copy_from_slice(&root.to_le_bytes());
        assert!(matches!(Plan::parse(&cycle), Err(Error::Invalid)));
        let mut outside = bytes.clone();
        outside[sub + 8..sub + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Plan::parse(&outside), Err(Error::Incomplete)));
        let mut wrong_type = bytes.clone();
        wrong_type[sub + 2..sub + 4].copy_from_slice(&5u16.to_le_bytes());
        assert!(matches!(Plan::parse(&wrong_type), Err(Error::Invalid)));

        for (id, expected2, expected3) in [
            (51009u16, Err(Error::Unsupported), Err(Error::Unsupported)),
            (51022, Ok(()), Err(Error::Unsupported)),
        ] {
            let mut modified = bytes.clone();
            let factor = root_field(&modified, 50734);
            modified[factor..factor + 2].copy_from_slice(&id.to_le_bytes());
            modified[factor + 2..factor + 4].copy_from_slice(&7u16.to_le_bytes());
            modified[factor + 4..factor + 8].copy_from_slice(&4u32.to_le_bytes());
            let plan = Plan::parse(&modified)
                .expect("stage 1 still valid")
                .expect("RGB16");
            let image = Image::new(modified, plan);
            assert_eq!(image.stage2_status(), expected2);
            assert_eq!(image.stage3_status(), expected3);
            let mut raw = [0u16; 9];
            image.row(0, &mut raw).expect("stage 1 preserved");
            assert_eq!(raw, SAMPLES[..9]);
            let mut untouched = [0xa5a5u16; 9];
            if expected2.is_err() {
                assert_eq!(image.stage2_row(0, &mut untouched), expected2);
                assert_eq!(untouched, [0xa5a5; 9]);
            }
            assert_eq!(image.stage3_row(0, &mut untouched), expected3);
            assert_eq!(untouched, [0xa5a5; 9]);
        }

        let mut raw_opcode3 = bytes.clone();
        let quality = field(&raw_opcode3, 50780);
        raw_opcode3[quality..quality + 2].copy_from_slice(&51022u16.to_le_bytes());
        raw_opcode3[quality + 2..quality + 4].copy_from_slice(&7u16.to_le_bytes());
        raw_opcode3[quality + 4..quality + 8].copy_from_slice(&4u32.to_le_bytes());
        let plan = Plan::parse(&raw_opcode3).expect("stage 1").expect("RGB16");
        let image = Image::new(raw_opcode3, plan);
        assert_eq!(image.stage2_status(), Ok(()));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));

        let mut unknown = bytes;
        let factor = root_field(&unknown, 50734);
        unknown[factor..factor + 2].copy_from_slice(&65000u16.to_le_bytes());
        assert!(matches!(Plan::parse(&unknown), Err(Error::Unsupported)));
    }

    #[test]
    fn rejects_partial_strips_bad_types_and_unhandled_processing() {
        let bytes = fixture(false, 7);
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

        let mut short = bytes.clone();
        let counts_tag = field(&short, 279);
        let counts = u32::from_le_bytes(
            short[counts_tag + 8..counts_tag + 12]
                .try_into()
                .expect("counts"),
        ) as usize;
        short[counts + 4..counts + 8].copy_from_slice(&17u32.to_le_bytes());
        assert!(matches!(Plan::parse(&short), Err(Error::Invalid)));

        let mut wrong_type = bytes.clone();
        wrong_type[offsets_tag + 2..offsets_tag + 4].copy_from_slice(&5u16.to_le_bytes());
        assert!(matches!(Plan::parse(&wrong_type), Err(Error::Invalid)));

        let mut wrong_count = bytes.clone();
        wrong_count[offsets_tag + 4..offsets_tag + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Plan::parse(&wrong_count), Err(Error::Invalid)));

        let mut unsupported = bytes.clone();
        let informational = field(&unsupported, 52548);
        unsupported[informational..informational + 2].copy_from_slice(&65000u16.to_le_bytes());
        assert!(matches!(Plan::parse(&unsupported), Err(Error::Unsupported)));

        let mut old_version = bytes;
        let version = field(&old_version, 50706);
        old_version[version + 9] = 4;
        assert!(matches!(Plan::parse(&old_version), Err(Error::Unsupported)));
    }

    #[test]
    fn later_processing_tags_preserve_stage1_but_gate_identity_stages() {
        let bytes = fixture(false, 7);
        let informational = field(&bytes, 52548);
        for (tag, stage2_supported, stage3_supported) in [
            (51009u16, false, false),
            (51022, true, false),
            (50713, true, true),
            (33421, false, false),
        ] {
            let mut modified = bytes.clone();
            modified[informational..informational + 2].copy_from_slice(&tag.to_le_bytes());
            if tag == 50713 || tag == 33421 {
                modified[informational + 2..informational + 4].copy_from_slice(&3u16.to_le_bytes());
                modified[informational + 4..informational + 8].copy_from_slice(&2u32.to_le_bytes());
                modified[informational + 8..informational + 12].copy_from_slice(&[1, 0, 1, 0]);
            }
            let plan = Plan::parse(&modified)
                .expect("valid stage 1")
                .expect("RGB16");
            let image = Image::new(modified, plan);
            let expected2 = if stage2_supported {
                Ok(())
            } else {
                Err(Error::Unsupported)
            };
            let expected3 = if stage3_supported {
                Ok(())
            } else {
                Err(Error::Unsupported)
            };
            assert_eq!(image.stage2_status(), expected2, "tag {tag}");
            assert_eq!(image.stage3_status(), expected3, "tag {tag}");
            let mut raw = [0u16; 9];
            image.row(0, &mut raw).expect("stage 1 preserved");
            assert_eq!(raw, SAMPLES[..9]);
            let mut output = [0xa5a5u16; 9];
            if stage2_supported {
                image.stage2_row(0, &mut output).expect("stage 2 identity");
                assert_eq!(output, SAMPLES[..9]);
                output.fill(0xa5a5);
            } else {
                assert_eq!(image.stage2_row(0, &mut output), expected2);
                assert_eq!(output, [0xa5a5; 9]);
            }
            assert_eq!(image.stage3_row(0, &mut output), expected3);
            if stage3_supported {
                assert_eq!(output, SAMPLES[..9]);
            } else {
                assert_eq!(output, [0xa5a5; 9]);
            }
        }

        let mut nonidentity = bytes.clone();
        let scale = field(&nonidentity, 50718);
        let data = u32::from_le_bytes(
            nonidentity[scale + 8..scale + 12]
                .try_into()
                .expect("scale pointer"),
        ) as usize;
        nonidentity[data..data + 4].copy_from_slice(&2u32.to_le_bytes());
        let plan = Plan::parse(&nonidentity).expect("stage 1").expect("RGB16");
        assert_eq!(
            Image::new(nonidentity, plan).stage2_status(),
            Err(Error::Unsupported)
        );

        let mut white = bytes;
        let field = field(&white, 50717);
        let data = u32::from_le_bytes(
            white[field + 8..field + 12]
                .try_into()
                .expect("white pointer"),
        ) as usize;
        white[data..data + 2].copy_from_slice(&65534u16.to_le_bytes());
        let plan = Plan::parse(&white).expect("stage 1").expect("RGB16");
        assert_eq!(
            Image::new(white, plan).stage2_status(),
            Err(Error::Unsupported)
        );
    }

    #[test]
    fn explicit_zero_black_is_identity_but_nonzero_black_is_not() {
        let mut bytes = fixture(false, 7);
        let at = field(&bytes, 52548);
        if bytes.len() & 1 != 0 {
            bytes.push(0);
        }
        let levels = bytes.len();
        bytes.extend_from_slice(&[0; 6]);
        bytes[at..at + 2].copy_from_slice(&50714u16.to_le_bytes());
        bytes[at + 2..at + 4].copy_from_slice(&3u16.to_le_bytes());
        bytes[at + 4..at + 8].copy_from_slice(&3u32.to_le_bytes());
        bytes[at + 8..at + 12].copy_from_slice(&(levels as u32).to_le_bytes());

        let plan = Plan::parse(&bytes).expect("valid stage 1").expect("RGB16");
        let image = Image::new(bytes.clone(), plan);
        assert_eq!(image.stage2_status(), Ok(()));
        assert_eq!(image.stage3_status(), Ok(()));
        let mut row = [0u16; 9];
        image.stage3_row(0, &mut row).expect("identity row");
        assert_eq!(row, SAMPLES[..9]);

        bytes[levels] = 1;
        let plan = Plan::parse(&bytes).expect("valid stage 1").expect("RGB16");
        let image = Image::new(bytes, plan);
        let mut untouched = [0xa5a5u16; 9];
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage2_row(0, &mut untouched), Err(Error::Unsupported));
        assert_eq!(untouched, [0xa5a5; 9]);
    }
}
