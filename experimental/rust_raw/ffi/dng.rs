// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Checked classic-TIFF DNG stage-1 reader for uncompressed 8/16-bit
//! monochrome LinearRaw strips. Final output is limited to one-strip 8-bit
//! scene-referred black/white or output-referred SDR monochrome.
//! Unrecognized tags are rejected: ignoring processing metadata would silently
//! turn a valid DNG into an incorrectly rendered image.

use super::linearization::{linearized_sample, LinearizationTable};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Invalid,
    Unsupported,
    Incomplete,
    OutOfMemory,
}

#[derive(Clone, Copy)]
pub(crate) enum ByteOrder {
    Little,
    Big,
}

impl ByteOrder {
    pub(crate) fn u16(self, b: &[u8]) -> u16 {
        match self {
            Self::Little => u16::from_le_bytes([b[0], b[1]]),
            Self::Big => u16::from_be_bytes([b[0], b[1]]),
        }
    }

    pub(crate) fn u32(self, b: &[u8]) -> u32 {
        match self {
            Self::Little => u32::from_le_bytes([b[0], b[1], b[2], b[3]]),
            Self::Big => u32::from_be_bytes([b[0], b[1], b[2], b[3]]),
        }
    }
}

pub(crate) fn range(data: &[u8], start: usize, size: usize) -> Result<&[u8], Error> {
    let end = start.checked_add(size).ok_or(Error::Invalid)?;
    data.get(start..end).ok_or(Error::Incomplete)
}

pub(crate) struct Tag<'a> {
    pub(crate) id: u16,
    pub(crate) kind: u16,
    pub(crate) count: u32,
    pub(crate) value: &'a [u8],
}

pub(crate) fn type_size(kind: u16) -> Option<usize> {
    match kind {
        1 | 2 | 6 | 7 => Some(1),
        3 | 8 => Some(2),
        4 | 9 | 11 => Some(4),
        5 | 10 | 12 => Some(8),
        _ => None,
    }
}

pub(crate) fn valid_sdr_tone_curve(tag: &Tag<'_>, order: ByteOrder) -> bool {
    if tag.kind != 11 || tag.count < 4 || tag.count % 2 != 0 {
        return false;
    }
    let mut previous = -1.0f32;
    for pair in tag.value.chunks_exact(8) {
        let x = f32::from_bits(order.u32(&pair[..4]));
        let y = f32::from_bits(order.u32(&pair[4..]));
        if !x.is_finite()
            || !y.is_finite()
            || !(0.0..=1.0).contains(&x)
            || !(0.0..=1.0).contains(&y)
            || x <= previous
        {
            return false;
        }
        if previous < 0.0 && (x != 0.0 || y != 0.0) {
            return false;
        }
        previous = x;
        if x == 1.0 && y != 1.0 {
            return false;
        }
    }
    previous == 1.0
}

fn check_type(id: u16, kind: u16) -> Result<(), Error> {
    let valid = match id {
        254 => kind == 4,
        256 | 257 | 273 | 278 | 279 | 50717 | 50829 => kind == 3 || kind == 4,
        258 | 259 | 262 | 274 | 277 | 284 | 339 | 50712 | 50713 | 50879 => kind == 3,
        270 | 271 | 272 | 305 | 306 | 315 | 33432 | 50708 => kind == 2,
        50706 | 50707 => kind == 1,
        50718 => kind == 5,
        50714 | 50719 | 50720 => kind == 3 || kind == 4 || kind == 5,
        50940 => kind == 11,
        51022 => kind == 7,
        51110 => kind == 4,
        _ => return Err(Error::Unsupported),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

fn tag<'a>(data: &'a [u8], entry: &'a [u8], order: ByteOrder) -> Result<Tag<'a>, Error> {
    let id = order.u16(&entry[0..2]);
    let kind = order.u16(&entry[2..4]);
    check_type(id, kind)?;
    tiff_tag(data, entry, order)
}

pub(crate) fn tiff_tag<'a>(
    data: &'a [u8],
    entry: &'a [u8],
    order: ByteOrder,
) -> Result<Tag<'a>, Error> {
    let id = order.u16(&entry[0..2]);
    let kind = order.u16(&entry[2..4]);
    let count = order.u32(&entry[4..8]);
    let unit = type_size(kind).ok_or(Error::Invalid)?;
    let size = usize::try_from(count)
        .ok()
        .and_then(|n| n.checked_mul(unit))
        .ok_or(Error::Invalid)?;
    let value = if size <= 4 {
        &entry[8..8 + size]
    } else {
        range(data, order.u32(&entry[8..12]) as usize, size)?
    };
    Ok(Tag {
        id,
        kind,
        count,
        value,
    })
}

pub(crate) fn number(tag: &Tag, index: usize, order: ByteOrder) -> Result<u32, Error> {
    if index >= tag.count as usize {
        return Err(Error::Invalid);
    }
    match tag.kind {
        3 => Ok(order.u16(range(
            tag.value,
            index.checked_mul(2).ok_or(Error::Invalid)?,
            2,
        )?) as u32),
        4 => Ok(order.u32(range(
            tag.value,
            index.checked_mul(4).ok_or(Error::Invalid)?,
            4,
        )?)),
        _ => Err(Error::Unsupported),
    }
}

pub(crate) fn scalar(tag: &Tag, order: ByteOrder) -> Result<u32, Error> {
    if tag.count != 1 {
        return Err(Error::Unsupported);
    }
    number(tag, 0, order)
}

pub(crate) fn integral(tag: &Tag, index: usize, order: ByteOrder) -> Result<u32, Error> {
    if tag.kind == 5 {
        let bytes = range(tag.value, index.checked_mul(8).ok_or(Error::Invalid)?, 8)?;
        let num = order.u32(&bytes[..4]);
        let den = order.u32(&bytes[4..]);
        if den == 0 || num % den != 0 {
            return Err(Error::Unsupported);
        }
        Ok(num / den)
    } else {
        number(tag, index, order)
    }
}

fn fraction(tag: &Tag, index: usize, order: ByteOrder) -> Result<(u64, u64), Error> {
    if index >= tag.count as usize {
        return Err(Error::Invalid);
    }
    if tag.kind != 5 {
        return Ok((u64::from(number(tag, index, order)?), 1));
    }
    let value = range(tag.value, index.checked_mul(8).ok_or(Error::Invalid)?, 8)?;
    let denominator = u64::from(order.u32(&value[4..]));
    if denominator == 0 {
        return Err(Error::Invalid);
    }
    Ok((u64::from(order.u32(&value[..4])), denominator))
}

pub(crate) fn optional<'tags, 'data>(
    tags: &'tags [Tag<'data>],
    id: u16,
) -> Option<&'tags Tag<'data>> {
    tags.binary_search_by_key(&id, |tag| tag.id)
        .ok()
        .map(|i| &tags[i])
}

pub(crate) fn required<'tags, 'data>(
    tags: &'tags [Tag<'data>],
    id: u16,
) -> Result<&'tags Tag<'data>, Error> {
    optional(tags, id).ok_or(Error::Invalid)
}

pub(crate) fn identity_array(
    tags: &[Tag<'_>],
    id: u16,
    values: &[u32],
    order: ByteOrder,
) -> Result<(), Error> {
    if let Some(tag) = optional(tags, id) {
        if tag.count as usize != values.len() {
            return Err(Error::Unsupported);
        }
        for (i, &expected) in values.iter().enumerate() {
            if integral(tag, i, order)? != expected {
                return Err(Error::Unsupported);
            }
        }
    }
    Ok(())
}

pub fn has_dng_version(data: &[u8]) -> bool {
    let Ok(header) = range(data, 0, 8) else {
        return false;
    };
    let order = match &header[..2] {
        b"II" => ByteOrder::Little,
        b"MM" => ByteOrder::Big,
        _ => return false,
    };
    if order.u16(&header[2..4]) != 42 {
        return false;
    }
    let start = order.u32(&header[4..8]) as usize;
    let Ok(count_bytes) = range(data, start, 2) else {
        return false;
    };
    let count = order.u16(count_bytes) as usize;
    let Some(entries_start) = start.checked_add(2) else {
        return false;
    };
    let Some(size) = count.checked_mul(12) else {
        return false;
    };
    let Ok(entries) = range(data, entries_start, size) else {
        return false;
    };
    entries.chunks_exact(12).any(|entry| {
        order.u16(&entry[..2]) == 50706
            && order.u16(&entry[2..4]) == 1
            && order.u32(&entry[4..8]) == 4
            && entry[8] == 1
    })
}

pub struct Image {
    bytes: Vec<u8>,
    strips: Vec<Strip>,
    order: ByteOrder,
    bits: u16,
    rows_per_strip: u32,
    black_repeat: [u32; 2],
    black: Vec<(u64, u64)>,
    max_black: (u64, u64),
    white: u32,
    linearization: Option<LinearizationTable>,
    stage2_supported: bool,
    stage3_supported: bool,
    final_render_supported: bool,
    output_referred_srgb: bool,
    pub width: u32,
    pub height: u32,
}

struct Strip {
    offset: usize,
}

impl Image {
    pub fn parse(bytes: Vec<u8>) -> Result<Self, Error> {
        let header = range(&bytes, 0, 8)?;
        let order = match &header[0..2] {
            b"II" => ByteOrder::Little,
            b"MM" => ByteOrder::Big,
            _ => return Err(Error::Invalid),
        };
        if order.u16(&header[2..4]) != 42 {
            return Err(Error::Unsupported);
        }
        let ifd_offset = order.u32(&header[4..8]) as usize;
        let count = order.u16(range(&bytes, ifd_offset, 2)?) as usize;
        let entries_start = ifd_offset.checked_add(2).ok_or(Error::Invalid)?;
        let entries_size = count.checked_mul(12).ok_or(Error::Invalid)?;
        range(&bytes, entries_start, entries_size)?;
        let next_offset = entries_start
            .checked_add(entries_size)
            .ok_or(Error::Invalid)?;
        if order.u32(range(&bytes, next_offset, 4)?) != 0 {
            return Err(Error::Unsupported);
        }

        let mut tags = Vec::new();
        tags.try_reserve_exact(count)
            .map_err(|_| Error::OutOfMemory)?;
        for i in 0..count {
            let entry_start = entries_start
                .checked_add(i.checked_mul(12).ok_or(Error::Invalid)?)
                .ok_or(Error::Invalid)?;
            let entry = range(&bytes, entry_start, 12)?;
            let item = tag(&bytes, entry, order)?;
            tags.push(item);
        }
        tags.sort_unstable_by_key(|item| item.id);
        if tags.windows(2).any(|pair| pair[0].id == pair[1].id) {
            return Err(Error::Invalid);
        }
        let version = required(&tags, 50706)?;
        let backward = required(&tags, 50707)?;
        if version.kind != 1 || version.count != 4 || backward.kind != 1 || backward.count != 4 {
            return Err(Error::Invalid);
        }
        if version.value != &[1, 4, 0, 0][..] || backward.value != &[1, 1, 0, 0][..] {
            return Err(Error::Unsupported);
        }
        let model = required(&tags, 50708)?;
        if model.kind != 2 || model.count < 2 || model.value.last() != Some(&0) {
            return Err(Error::Invalid);
        }
        for id in [270, 271, 272, 305, 306, 315, 33432] {
            if let Some(text) = optional(&tags, id) {
                if text.kind != 2 || text.value.last() != Some(&0) {
                    return Err(Error::Invalid);
                }
            }
        }
        let width = scalar(required(&tags, 256)?, order)?;
        let height = scalar(required(&tags, 257)?, order)?;
        // SDK's qDNGBigImage maximum dimension. Conversion to SkISize is safe.
        if width == 0 || height == 0 || width > 300_000 || height > 300_000 {
            return Err(Error::Invalid);
        }
        let is = |id, expected| -> Result<(), Error> {
            if scalar(required(&tags, id)?, order)? == expected {
                Ok(())
            } else {
                Err(Error::Unsupported)
            }
        };
        let bits = scalar(required(&tags, 258)?, order)?;
        if bits != 8 && bits != 16 {
            return Err(Error::Unsupported);
        }
        is(259, 1)?;
        is(262, 34892)?;
        is(277, 1)?;
        if let Some(tag) = optional(&tags, 254) {
            if scalar(tag, order)? != 0 {
                return Err(Error::Unsupported);
            }
        }
        for (id, expected) in [(274, 1), (284, 1), (339, 1)] {
            if let Some(tag) = optional(&tags, id) {
                if integral(tag, 0, order)? != expected || tag.count != 1 {
                    return Err(Error::Unsupported);
                }
            }
        }
        identity_array(&tags, 50719, &[0, 0], order)?;
        identity_array(&tags, 50720, &[width, height], order)?;
        identity_array(&tags, 50829, &[0, 0, height, width], order)?;
        identity_array(&tags, 50718, &[1, 1], order)?;

        let mut black_repeat = [1, 1];
        if let Some(dimensions) = optional(&tags, 50713) {
            if dimensions.count != 2 {
                return Err(Error::Invalid);
            }
            for (i, dimension) in black_repeat.iter_mut().enumerate() {
                *dimension = number(dimensions, i, order)?;
                if *dimension == 0 || *dimension > 8 {
                    return Err(Error::Unsupported);
                }
            }
        }
        let black_count = (black_repeat[0] as usize)
            .checked_mul(black_repeat[1] as usize)
            .ok_or(Error::Invalid)?;
        let mut black = Vec::new();
        black
            .try_reserve_exact(black_count)
            .map_err(|_| Error::OutOfMemory)?;
        if let Some(levels) = optional(&tags, 50714) {
            if levels.count as usize != black_count {
                return Err(Error::Invalid);
            }
            for i in 0..black_count {
                black.push(fraction(levels, i, order)?);
            }
        } else if black_count == 1 {
            black.push((0, 1));
        } else {
            return Err(Error::Unsupported);
        }

        let max_sample = (1u32 << bits) - 1;
        let white = if let Some(level) = optional(&tags, 50717) {
            scalar(level, order)?
        } else {
            max_sample
        };
        if white == 0 || white > max_sample {
            return Err(Error::Invalid);
        }
        if black
            .iter()
            .any(|&(numerator, denominator)| numerator >= u64::from(white) * denominator)
        {
            return Err(Error::Invalid);
        }
        let max_black = black.iter().copied().try_fold(
            (0u64, 1u64),
            |highest, current| -> Result<(u64, u64), Error> {
                let left = u128::from(current.0)
                    .checked_mul(u128::from(highest.1))
                    .ok_or(Error::Invalid)?;
                let right = u128::from(highest.0)
                    .checked_mul(u128::from(current.1))
                    .ok_or(Error::Invalid)?;
                Ok(if left > right { current } else { highest })
            },
        )?;
        let linearization = optional(&tags, 50712)
            .map(|tag| LinearizationTable::parse(tag, order))
            .transpose()?;
        let stage2_supported = linearization.as_ref().is_none_or(|table| {
            black.iter().all(|&(numerator, _)| numerator == 0)
                && if bits == 8 {
                    white == u32::from(u8::MAX) && table.maps_to_sdr_8_bit()
                } else {
                    white == u32::from(u16::MAX) && table.has_identity_endpoints()
                }
        });

        let rows_per_strip = scalar(required(&tags, 278)?, order)?;
        if rows_per_strip == 0 {
            return Err(Error::Invalid);
        }
        let strip_count = 1 + (height - 1) / rows_per_strip;
        let offsets = required(&tags, 273)?;
        let sizes = required(&tags, 279)?;
        if offsets.count != strip_count || sizes.count != strip_count {
            return Err(Error::Invalid);
        }
        let row_bytes = (width as usize)
            .checked_mul((bits / 8) as usize)
            .ok_or(Error::Invalid)?;
        let mut strips = Vec::new();
        strips
            .try_reserve_exact(strip_count as usize)
            .map_err(|_| Error::OutOfMemory)?;
        for i in 0..strip_count {
            let row_start = i.checked_mul(rows_per_strip).ok_or(Error::Invalid)?;
            let rows = (height - row_start).min(rows_per_strip);
            let expected = (rows as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            let declared = number(sizes, i as usize, order)? as usize;
            if declared != expected {
                return Err(Error::Invalid);
            }
            let offset = number(offsets, i as usize, order)? as usize;
            range(&bytes, offset, declared)?;
            strips.push(Strip { offset });
        }
        let stage3_supported = stage2_supported && optional(&tags, 51022).is_none();
        let colorimetric = if let Some(reference) = optional(&tags, 50879) {
            if reference.count != 1 {
                return Err(Error::Invalid);
            }
            match scalar(reference, order)? {
                0 => 0,
                1 => 1,
                _ => return Err(Error::Unsupported),
            }
        } else {
            0
        };
        let output_referred_srgb = colorimetric == 1;
        let has_tone = if let Some(tone) = optional(&tags, 50940) {
            if !valid_sdr_tone_curve(tone, order) {
                return Err(Error::Invalid);
            }
            true
        } else {
            false
        };
        let black_render = optional(&tags, 51110)
            .map(|tag| scalar(tag, order))
            .transpose()?;
        if black_render.is_some_and(|value| value > 1) {
            return Err(Error::Invalid);
        }
        let supported_final_profile = if has_tone || black_render.is_some() {
            output_referred_srgb && has_tone && black_render == Some(1)
        } else {
            true
        };
        let final_render_supported = if stage3_supported
            && linearization
                .as_ref()
                .is_none_or(LinearizationTable::maps_to_sdr_8_bit)
            && bits == 8
            && black.as_slice() == [(0, 1)]
            && white == 255
            && supported_final_profile
        {
            if output_referred_srgb {
                true
            } else {
                let mut binary = true;
                for (index, strip) in strips.iter().enumerate() {
                    let length = number(sizes, index, order)? as usize;
                    let pixels = range(&bytes, strip.offset, length)?;
                    if pixels.iter().any(|&sample| sample != 0 && sample != 255) {
                        binary = false;
                        break;
                    }
                }
                binary
            }
        } else {
            false
        };
        Ok(Self {
            bytes,
            strips,
            order,
            bits: bits as u16,
            rows_per_strip,
            black_repeat,
            black,
            max_black,
            white,
            linearization,
            stage2_supported,
            stage3_supported,
            final_render_supported,
            output_referred_srgb,
            width,
            height,
        })
    }

    pub fn bits_per_sample(&self) -> u16 {
        self.bits
    }

    pub fn supports_final_render(&self) -> bool {
        self.final_render_supported
    }

    pub fn stage2_status(&self) -> Result<(), Error> {
        if self.stage2_supported {
            Ok(())
        } else {
            Err(Error::Unsupported)
        }
    }

    pub fn stage3_status(&self) -> Result<(), Error> {
        if self.stage3_supported {
            Ok(())
        } else {
            Err(Error::Unsupported)
        }
    }

    pub fn stage3_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage3_status()?;
        self.normalized_row(row, output)
    }

    fn row_bytes(&self, row: u32) -> Result<&[u8], Error> {
        if row >= self.height {
            return Err(Error::Invalid);
        }
        let row_size = (self.width as usize)
            .checked_mul((self.bits / 8) as usize)
            .ok_or(Error::Invalid)?;
        let strip = &self.strips[(row / self.rows_per_strip) as usize];
        let start = ((row % self.rows_per_strip) as usize)
            .checked_mul(row_size)
            .and_then(|n| strip.offset.checked_add(n))
            .ok_or(Error::Invalid)?;
        range(&self.bytes, start, row_size)
    }

    pub fn raw_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        if output.len() != self.width as usize {
            return Err(Error::Invalid);
        }
        let pixels = self.row_bytes(row)?;
        if self.bits == 8 {
            for (&pixel, sample) in pixels.iter().zip(output.iter_mut()) {
                *sample = u16::from(pixel);
            }
        } else {
            for (pixel, sample) in pixels.chunks_exact(2).zip(output.iter_mut()) {
                *sample = self.order.u16(pixel);
            }
        }
        Ok(())
    }

    pub fn normalized_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage2_status()?;
        self.raw_row(row, output)?;
        for (column, sample) in output.iter_mut().enumerate() {
            let black_index = ((row % self.black_repeat[0]) * self.black_repeat[1]
                + (column as u32 % self.black_repeat[1])) as usize;
            let (numerator, denominator) = self.black[black_index];
            let value = u128::from(linearized_sample(self.linearization.as_ref(), *sample))
                * u128::from(denominator);
            let low = u128::from(numerator);
            if value <= low {
                *sample = 0;
                continue;
            }
            // DNG uses the plane's maximum black level in the denominator,
            // while subtracting the local repeating black level per pixel.
            let (max_num, max_den) = self.max_black;
            let span = u128::from(self.white)
                .checked_mul(u128::from(max_den))
                .and_then(|white| white.checked_sub(u128::from(max_num)))
                .and_then(|white| white.checked_mul(u128::from(denominator)))
                .ok_or(Error::Invalid)?;
            let scaled = (value - low)
                .checked_mul(u128::from(max_den))
                .and_then(|value| value.checked_mul(u128::from(u16::MAX)))
                .ok_or(Error::Invalid)?;
            let saturation = span
                .checked_mul(u128::from(u16::MAX))
                .ok_or(Error::Invalid)?;
            *sample = if scaled >= saturation {
                u16::MAX
            } else {
                (scaled.checked_add(span / 2).ok_or(Error::Invalid)? / span) as u16
            };
        }
        Ok(())
    }

    pub fn copy_rgb_row(&self, row: u32, output: &mut [u8]) -> Result<(), Error> {
        if !self.final_render_supported {
            return Err(Error::Unsupported);
        }
        let expected = (self.width as usize).checked_mul(3).ok_or(Error::Invalid)?;
        if output.len() != expected {
            return Err(Error::Invalid);
        }
        let pixels = self.row_bytes(row)?;
        for (&pixel, rgb) in pixels.iter().zip(output.chunks_exact_mut(3)) {
            // Final-render gating guarantees the table maps into u8.
            let mapped = linearized_sample(self.linearization.as_ref(), u16::from(pixel)) as u8;
            rgb.fill(if self.output_referred_srgb {
                srgb_from_linear_u8(mapped)
            } else {
                mapped
            });
        }
        Ok(())
    }
}

pub(crate) fn srgb_from_linear_u8(sample: u8) -> u8 {
    let linear = f64::from(sample) / 255.0;
    let encoded = if linear <= 0.0031308 {
        12.92 * linear
    } else {
        1.055 * linear.powf(1.0 / 2.4) - 0.055
    };
    (encoded * 255.0).round() as u8
}

#[cfg(test)]
mod tests {
    use super::{has_dng_version, srgb_from_linear_u8, Error, Image};

    fn fixture(big_endian: bool) -> Vec<u8> {
        let tags: &[(u16, u16, u32, u32)] = &[
            (256, 4, 1, 2),
            (257, 4, 1, 2),
            (258, 3, 1, 8),
            (259, 3, 1, 1),
            (262, 3, 1, 34892),
            (273, 4, 1, 0),
            (277, 3, 1, 1),
            (278, 4, 1, 2),
            (279, 4, 1, 4),
            (50706, 1, 4, 0),
            (50707, 1, 4, 0),
            (50708, 2, 5, 0),
        ];
        let mut bytes = if big_endian {
            b"MM\0\x2a\0\0\0\x08".to_vec()
        } else {
            b"II\x2a\0\x08\0\0\0".to_vec()
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
        bytes.extend_from_slice(&word(tags.len() as u16));
        let model_offset = 8 + 2 + tags.len() * 12 + 4;
        for &(id, kind, count, value) in tags {
            bytes.extend_from_slice(&word(id));
            bytes.extend_from_slice(&word(kind));
            bytes.extend_from_slice(&long(count));
            match id {
                273 => bytes.extend_from_slice(&long((model_offset + 6) as u32)),
                50706 => bytes.extend_from_slice(&[1, 4, 0, 0]),
                50707 => bytes.extend_from_slice(&[1, 1, 0, 0]),
                50708 => bytes.extend_from_slice(&long(model_offset as u32)),
                _ if kind == 3 => {
                    bytes.extend_from_slice(&word(value as u16));
                    bytes.extend_from_slice(&[0, 0]);
                }
                _ => bytes.extend_from_slice(&long(value)),
            }
        }
        bytes.extend_from_slice(&long(0));
        bytes.extend_from_slice(b"mono\0\0");
        bytes.extend_from_slice(&[0, 255, 255, 0]);
        bytes
    }

    fn insert_colorimetric(bytes: &mut Vec<u8>, value: u16) {
        let count = u16::from_le_bytes(bytes[8..10].try_into().expect("IFD count")) as usize;
        let next = 10 + count * 12;
        let mut entry = Vec::new();
        entry.extend_from_slice(&50879u16.to_le_bytes());
        entry.extend_from_slice(&3u16.to_le_bytes());
        entry.extend_from_slice(&1u32.to_le_bytes());
        entry.extend_from_slice(&value.to_le_bytes());
        entry.extend_from_slice(&[0; 2]);
        bytes.splice(next..next, entry);
        bytes[8..10].copy_from_slice(&((count + 1) as u16).to_le_bytes());
        for entry in [10 + 5 * 12 + 8, 10 + 11 * 12 + 8] {
            let offset =
                u32::from_le_bytes(bytes[entry..entry + 4].try_into().expect("tag offset"));
            bytes[entry..entry + 4].copy_from_slice(&(offset + 12).to_le_bytes());
        }
    }

    fn split_fixture_into_rows(bytes: &mut Vec<u8>, big_endian: bool) -> usize {
        let long = |n: u32| {
            if big_endian {
                n.to_be_bytes()
            } else {
                n.to_le_bytes()
            }
        };
        let offset_entry = 10 + 5 * 12;
        let rows_entry = 10 + 7 * 12;
        let lengths_entry = 10 + 8 * 12;
        let raw_offset = if big_endian {
            u32::from_be_bytes(
                bytes[offset_entry + 8..offset_entry + 12]
                    .try_into()
                    .expect("raw offset"),
            )
        } else {
            u32::from_le_bytes(
                bytes[offset_entry + 8..offset_entry + 12]
                    .try_into()
                    .expect("raw offset"),
            )
        };
        bytes[rows_entry + 8..rows_entry + 12].copy_from_slice(&long(1));
        bytes[offset_entry + 4..offset_entry + 8].copy_from_slice(&long(2));
        bytes[lengths_entry + 4..lengths_entry + 8].copy_from_slice(&long(2));
        let offsets = bytes.len() as u32;
        bytes.extend_from_slice(&long(raw_offset));
        bytes.extend_from_slice(&long(raw_offset + 2));
        let lengths = bytes.len() as u32;
        bytes.extend_from_slice(&long(2));
        bytes.extend_from_slice(&long(2));
        bytes[offset_entry + 8..offset_entry + 12].copy_from_slice(&long(offsets));
        bytes[lengths_entry + 8..lengths_entry + 12].copy_from_slice(&long(lengths));
        raw_offset as usize
    }

    fn output_mono_with_tone() -> (Vec<u8>, usize, usize) {
        let mut bytes = fixture(false);
        *bytes.last_mut().expect("image has pixels") = 128;
        insert_colorimetric(&mut bytes, 1);
        let count = u16::from_le_bytes(bytes[8..10].try_into().expect("count")) as usize;
        let entries = bytes[10..10 + count * 12].to_vec();
        while bytes.len() & 3 != 0 {
            bytes.push(0);
        }
        let new_ifd = bytes.len();
        let tone_entry = new_ifd + 2 + entries.len();
        let black_entry = tone_entry + 12;
        let tone_data = new_ifd + 2 + (count + 2) * 12 + 4;
        bytes.extend_from_slice(&((count + 2) as u16).to_le_bytes());
        bytes.extend_from_slice(&entries);
        for (id, kind, length, value) in [
            (50940u16, 11u16, 6u32, tone_data as u32),
            (51110u16, 4u16, 1u32, 1u32),
        ] {
            bytes.extend_from_slice(&id.to_le_bytes());
            bytes.extend_from_slice(&kind.to_le_bytes());
            bytes.extend_from_slice(&length.to_le_bytes());
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        bytes.extend_from_slice(&0u32.to_le_bytes());
        assert_eq!(bytes.len(), tone_data);
        for value in [0.0f32, 0.0, 0.5, 0.7, 1.0, 1.0] {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        bytes[4..8].copy_from_slice(&(new_ifd as u32).to_le_bytes());
        (bytes, tone_entry, black_entry)
    }

    #[test]
    fn decodes_checked_rows() {
        for bytes in [fixture(false), fixture(true)] {
            assert!(has_dng_version(&bytes));
            let image = Image::parse(bytes).unwrap_or_else(|error| panic!("{error:?}"));
            assert_eq!((image.width, image.height), (2, 2));
            let mut row = [0xaa; 6];
            assert_eq!(image.copy_rgb_row(0, &mut row), Ok(()));
            assert_eq!(row, [0, 0, 0, 255, 255, 255]);
            assert_eq!(image.copy_rgb_row(1, &mut row), Ok(()));
            assert_eq!(row, [255, 255, 255, 0, 0, 0]);
            assert_eq!(image.copy_rgb_row(2, &mut row), Err(Error::Invalid));
            assert_eq!(row, [255, 255, 255, 0, 0, 0]);
            let mut wrong_size = [0xa5; 5];
            assert_eq!(image.copy_rgb_row(0, &mut wrong_size), Err(Error::Invalid));
            assert_eq!(wrong_size, [0xa5; 5]);
            let mut samples = [0; 2];
            image.raw_row(1, &mut samples).expect("raw row");
            assert_eq!(samples, [255, 0]);
            image
                .normalized_row(1, &mut samples)
                .expect("normalized row");
            assert_eq!(samples, [65535, 0]);
            assert_eq!(image.stage3_status(), Ok(()));
            image.stage3_row(1, &mut samples).expect("identity Stage 3");
            assert_eq!(samples, [65535, 0]);
        }
    }

    #[test]
    fn output_referred_sdr_transfers_checked_8bit_midtones() {
        for (linear, encoded) in [
            (0, 0),
            (1, 13),
            (2, 22),
            (3, 28),
            (4, 34),
            (8, 50),
            (16, 71),
            (32, 99),
            (64, 137),
            (128, 188),
            (192, 225),
            (255, 255),
        ] {
            assert_eq!(srgb_from_linear_u8(linear), encoded);
        }
        let mut scene = fixture(false);
        *scene.last_mut().expect("fixture pixels") = 128;
        assert!(!Image::parse(scene.clone())
            .expect("scene stage 1")
            .supports_final_render());

        insert_colorimetric(&mut scene, 1);
        let image = Image::parse(scene.clone()).expect("output-referred DNG");
        assert!(image.supports_final_render());
        let mut row = [0u8; 6];
        assert_eq!(image.copy_rgb_row(1, &mut row), Ok(()));
        assert_eq!(row, [255, 255, 255, 188, 188, 188]);
        let mut raw = [0u16; 2];
        image.raw_row(1, &mut raw).expect("stage 1 unchanged");
        assert_eq!(raw, [255, 128]);
        image.stage3_row(1, &mut raw).expect("stage 3 unchanged");
        assert_eq!(raw, [65535, 32896]);

        let mut hdr = fixture(false);
        insert_colorimetric(&mut hdr, 2);
        assert!(matches!(Image::parse(hdr), Err(Error::Unsupported)));
        let mut wrong_type = scene.clone();
        let tag = 10 + 12 * 12;
        wrong_type[tag + 2..tag + 4].copy_from_slice(&4u16.to_le_bytes());
        assert!(matches!(Image::parse(wrong_type), Err(Error::Invalid)));
        let mut wrong_count = scene.clone();
        wrong_count[tag + 4..tag + 8].copy_from_slice(&2u32.to_le_bytes());
        assert!(matches!(Image::parse(wrong_count), Err(Error::Invalid)));
        insert_colorimetric(&mut scene, 1);
        assert!(matches!(Image::parse(scene), Err(Error::Invalid)));
    }

    #[test]
    fn monochrome_final_rows_cross_checked_strip_boundaries() {
        for (big_endian, output_referred) in [(false, false), (true, false), (false, true)] {
            let mut bytes = fixture(big_endian);
            if output_referred {
                *bytes.last_mut().expect("final sample") = 128;
                insert_colorimetric(&mut bytes, 1);
            }
            let raw_offset = split_fixture_into_rows(&mut bytes, big_endian);
            let image = Image::parse(bytes.clone()).expect("two valid strips");
            assert_eq!(image.stage3_status(), Ok(()));
            assert!(image.supports_final_render());
            for (row, expected) in [[0, 255], [255, if output_referred { 188 } else { 0 }]]
                .into_iter()
                .enumerate()
            {
                let mut rgb = [0xa5; 8];
                assert_eq!(image.copy_rgb_row(row as u32, &mut rgb[..6]), Ok(()));
                for (x, &color) in expected.iter().enumerate() {
                    assert_eq!(&rgb[x * 3..x * 3 + 3], &[color; 3]);
                }
                assert_eq!(rgb[6..], [0xa5; 2]);
            }
            if !output_referred {
                bytes[raw_offset + 3] = 128;
                let image = Image::parse(bytes).expect("valid scene strips with midtone");
                assert_eq!(image.stage3_status(), Ok(()));
                assert!(!image.supports_final_render());
                let mut rgb = [0xa5; 6];
                assert_eq!(image.copy_rgb_row(0, &mut rgb), Err(Error::Unsupported));
                assert_eq!(rgb, [0xa5; 6]);
            }
        }
    }

    #[test]
    fn validated_color_tone_is_ignored_for_output_referred_monochrome() {
        let (bytes, tone, black) = output_mono_with_tone();
        let image = Image::parse(bytes.clone()).expect("output-referred mono");
        assert!(image.supports_final_render());
        assert_eq!(image.stage3_status(), Ok(()));
        let mut rgb = [0xa5; 6];
        assert_eq!(image.copy_rgb_row(1, &mut rgb), Ok(()));
        assert_eq!(rgb, [255, 255, 255, 188, 188, 188]);

        let mut invalid_tone = bytes.clone();
        let tone_at = u32::from_le_bytes(
            invalid_tone[tone + 8..tone + 12]
                .try_into()
                .expect("tone pointer"),
        ) as usize;
        invalid_tone[tone_at + 8..tone_at + 12].copy_from_slice(&f32::NAN.to_le_bytes());
        assert!(matches!(Image::parse(invalid_tone), Err(Error::Invalid)));

        let mut automatic_black = bytes.clone();
        automatic_black[black + 8..black + 12].copy_from_slice(&0u32.to_le_bytes());
        let image = Image::parse(automatic_black).expect("known Auto value");
        assert_eq!(image.stage3_status(), Ok(()));
        assert!(!image.supports_final_render());
        rgb.fill(0xa5);
        assert_eq!(image.copy_rgb_row(1, &mut rgb), Err(Error::Unsupported));
        assert_eq!(rgb, [0xa5; 6]);

        let mut invalid_black = bytes;
        invalid_black[black + 8..black + 12].copy_from_slice(&2u32.to_le_bytes());
        assert!(matches!(Image::parse(invalid_black), Err(Error::Invalid)));
    }

    #[test]
    fn monochrome_opcode_list3_preserves_stage2_but_blocks_stage3_and_final() {
        let mut bytes = fixture(false);
        let next_ifd = 10 + 12 * 12;
        let mut opcode = Vec::new();
        opcode.extend_from_slice(&51022u16.to_le_bytes());
        opcode.extend_from_slice(&7u16.to_le_bytes());
        opcode.extend_from_slice(&4u32.to_le_bytes());
        opcode.extend_from_slice(&[0; 4]);
        bytes.splice(next_ifd..next_ifd, opcode);
        bytes[8..10].copy_from_slice(&13u16.to_le_bytes());
        for entry in [10 + 5 * 12 + 8, 10 + 11 * 12 + 8] {
            let offset =
                u32::from_le_bytes(bytes[entry..entry + 4].try_into().expect("tag offset"));
            bytes[entry..entry + 4].copy_from_slice(&(offset + 12).to_le_bytes());
        }
        let image = Image::parse(bytes).expect("valid mono stage 1 and 2");
        assert!(!image.supports_final_render());
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut normalized = [0u16; 2];
        image.normalized_row(1, &mut normalized).expect("stage 2");
        assert_eq!(normalized, [65535, 0]);
        let mut untouched = [0xa5a5u16; 2];
        assert_eq!(image.stage3_row(1, &mut untouched), Err(Error::Unsupported));
        assert_eq!(untouched, [0xa5a5; 2]);
    }

    #[test]
    fn rejects_unsupported_features_and_malformed_offsets() {
        let bytes = fixture(false);
        assert!(!has_dng_version(&bytes[..8]));
        assert_eq!(
            Image::parse(bytes[..bytes.len() - 1].to_vec()).err(),
            Some(Error::Incomplete)
        );
        let mut cfa = bytes.clone();
        cfa[8 + 2 + 4 * 12 + 8..8 + 2 + 4 * 12 + 10].copy_from_slice(&32803u16.to_le_bytes());
        assert_eq!(Image::parse(cfa).err(), Some(Error::Unsupported));
        let mut bad_offset = bytes.clone();
        bad_offset[8 + 2 + 5 * 12 + 8..8 + 2 + 5 * 12 + 12]
            .copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(Image::parse(bad_offset).err(), Some(Error::Incomplete));
        let mut gray = bytes.clone();
        *gray.last_mut().expect("fixture has pixels") = 128;
        let image = Image::parse(gray).expect("valid stage-1 grayscale");
        assert!(!image.supports_final_render());
        let mut samples = [0u16; 2];
        image.raw_row(1, &mut samples).expect("stage-1 row");
        assert_eq!(samples, [255, 128]);
        let mut tiff = bytes;
        tiff[8 + 2 + 9 * 12..8 + 2 + 9 * 12 + 2].copy_from_slice(&50705u16.to_le_bytes());
        assert!(!has_dng_version(&tiff));
        assert_eq!(Image::parse(tiff).err(), Some(Error::Unsupported));
    }

    #[test]
    fn rejects_wrong_types_before_resolving_values() {
        let ids = [
            256, 257, 258, 259, 262, 273, 277, 278, 279, 50706, 50707, 50708,
        ];
        for (id, kind) in [
            (256, 5u16),
            (258, 4),
            (259, 4),
            (262, 4),
            (273, 5),
            (277, 4),
            (278, 5),
            (279, 5),
            (50706, 4),
            (50708, 1),
        ] {
            let mut bytes = fixture(false);
            let index = ids.iter().position(|&tag| tag == id).expect("test tag");
            let type_offset = 10 + index * 12 + 2;
            bytes[type_offset..type_offset + 2].copy_from_slice(&kind.to_le_bytes());
            assert_eq!(Image::parse(bytes).err(), Some(Error::Invalid), "tag {id}");
        }
    }

    fn multi_strip_fixture(big_endian: bool, bits: u16) -> Vec<u8> {
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
        let samples: [u16; 6] = if bits == 8 {
            [0, 10, 255, 20, 128, 230]
        } else {
            [0, 1024, 65535, 2048, 32768, 60000]
        };
        let black: [u32; 2] = if bits == 8 { [0, 0] } else { [1024, 2048] };
        let white = if bits == 8 { 255 } else { 60000 };
        let mut levels = Vec::new();
        for value in black {
            levels.extend_from_slice(&long(value));
            levels.extend_from_slice(&long(1));
        }
        let mut sizes = Vec::new();
        sizes.extend_from_slice(&long(u32::from(bits / 8) * 4));
        sizes.extend_from_slice(&long(u32::from(bits / 8) * 2));
        let mut fields = vec![
            (254, 4, 1, long(0).to_vec()),
            (256, 4, 1, long(2).to_vec()),
            (257, 4, 1, long(3).to_vec()),
            (258, 3, 1, word(bits).to_vec()),
            (259, 3, 1, word(1).to_vec()),
            (262, 3, 1, word(34892).to_vec()),
            (273, 4, 2, vec![0u8; 8]),
            (277, 3, 1, word(1).to_vec()),
            (278, 4, 1, long(2).to_vec()),
            (279, 4, 2, sizes),
            (50706, 1, 4, vec![1, 4, 0, 0]),
            (50707, 1, 4, vec![1, 1, 0, 0]),
            (50708, 2, 6, b"stage\0".to_vec()),
            (50713, 3, 2, [word(2), word(1)].concat()),
            (50714, 5, 2, levels),
            (50717, 4, 1, long(white).to_vec()),
        ];
        fields.sort_unstable_by_key(|field| field.0);
        let mut bytes = if big_endian {
            b"MM\0\x2a\0\0\0\x08".to_vec()
        } else {
            b"II\x2a\0\x08\0\0\0".to_vec()
        };
        bytes.extend_from_slice(&word(fields.len() as u16));
        let entry_start = bytes.len();
        bytes.resize(entry_start + fields.len() * 12 + 4, 0);
        let mut offset_array = 0;
        let mut size_array = 0;
        for (i, (id, kind, count, value)) in fields.iter().enumerate() {
            let entry = entry_start + i * 12;
            bytes[entry..entry + 2].copy_from_slice(&word(*id));
            bytes[entry + 2..entry + 4].copy_from_slice(&word(*kind));
            bytes[entry + 4..entry + 8].copy_from_slice(&long(*count));
            if value.len() <= 4 {
                bytes[entry + 8..entry + 8 + value.len()].copy_from_slice(value);
            } else {
                if bytes.len() & 1 != 0 {
                    bytes.push(0);
                }
                let offset = bytes.len();
                bytes[entry + 8..entry + 12].copy_from_slice(&long(offset as u32));
                bytes.extend_from_slice(value);
                if *id == 273 {
                    offset_array = offset;
                }
                if *id == 279 {
                    size_array = offset;
                }
            }
        }
        assert!(offset_array != 0 && size_array != 0);
        for (strip, row_count) in [2usize, 1].into_iter().enumerate() {
            if bytes.len() & 1 != 0 {
                bytes.push(0);
            }
            let offset = bytes.len() as u32;
            bytes[offset_array + strip * 4..offset_array + strip * 4 + 4]
                .copy_from_slice(&long(offset));
            let first = if strip == 0 { 0 } else { 4 };
            for &sample in &samples[first..first + row_count * 2] {
                if bits == 8 {
                    bytes.push(sample as u8);
                } else {
                    bytes.extend_from_slice(&word(sample));
                }
            }
        }
        bytes
    }

    fn linearized_mono_fixture(big_endian: bool, short: bool) -> Vec<u8> {
        let mut bytes = multi_strip_fixture(big_endian, 16);
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
        let read_long = |bytes: &[u8], at: usize| {
            (if big_endian {
                u32::from_be_bytes(bytes[at..at + 4].try_into().expect("offset"))
            } else {
                u32::from_le_bytes(bytes[at..at + 4].try_into().expect("offset"))
            }) as usize
        };
        let count = if big_endian {
            u16::from_be_bytes(bytes[8..10].try_into().expect("count"))
        } else {
            u16::from_le_bytes(bytes[8..10].try_into().expect("count"))
        };
        let find = |data: &[u8], id: u16| {
            (0..count as usize)
                .map(|i| 10 + i * 12)
                .find(|&at| data[at..at + 2] == word(id))
                .expect("known tag")
        };
        let black = read_long(&bytes, find(&bytes, 50714) + 8);
        for offset in [black, black + 8] {
            bytes[offset..offset + 4].copy_from_slice(&long(0));
        }
        let white = find(&bytes, 50717);
        bytes[white + 8..white + 12].copy_from_slice(&long(65535));
        let mut fields: Vec<[u8; 12]> = bytes[10..10 + 12 * count as usize]
            .chunks_exact(12)
            .map(|entry| entry.try_into().expect("tag"))
            .collect();
        let value = if short {
            [word(0), word(u16::MAX)].concat()
        } else {
            if bytes.len() & 1 != 0 {
                bytes.push(0);
            }
            let offset = bytes.len();
            for sample in 0..=u16::MAX {
                bytes.extend_from_slice(&word(sample.saturating_mul(2)));
            }
            long(offset as u32).to_vec()
        };
        let tag: [u8; 12] = [
            word(50712).as_slice(),
            word(3).as_slice(),
            long(if short { 2 } else { 65536 }).as_slice(),
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

    #[test]
    fn linearization_preserves_raw_and_maps_mono16_at_stage2() {
        for big_endian in [false, true] {
            for short in [false, true] {
                let image = Image::parse(linearized_mono_fixture(big_endian, short))
                    .expect("checked mono16");
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Ok(()));
                assert!(!image.supports_final_render());
                for (y, expected) in [[0u16, 1024], [65535, 2048], [32768, 60000]]
                    .into_iter()
                    .enumerate()
                {
                    let mut raw = [0xa5a5; 3];
                    let mut mapped = [0xa5a5; 3];
                    image.raw_row(y as u32, &mut raw[..2]).expect("Stage1");
                    assert_eq!(raw[..2], expected);
                    image
                        .normalized_row(y as u32, &mut mapped[..2])
                        .expect("Stage2");
                    for i in 0..2 {
                        assert_eq!(
                            mapped[i],
                            if short {
                                if raw[i] == 0 {
                                    0
                                } else {
                                    u16::MAX
                                }
                            } else {
                                raw[i].saturating_mul(2)
                            }
                        );
                    }
                    assert_eq!((raw[2], mapped[2]), (0xa5a5, 0xa5a5));
                    let expected = mapped;
                    mapped.fill(0xa5a5);
                    image
                        .stage3_row(y as u32, &mut mapped[..2])
                        .expect("Stage3");
                    assert_eq!(mapped[..2], expected[..2]);
                }
            }
        }
    }

    #[test]
    fn invalid_mono_linearization_keeps_stage2_unsupported_without_publishing() {
        let good = linearized_mono_fixture(false, false);
        let first = u32::from_le_bytes(good[4..8].try_into().expect("IFD")) as usize;
        let count = u16::from_le_bytes(good[first..first + 2].try_into().expect("count")) as usize;
        let table = (0..count)
            .map(|i| first + 2 + i * 12)
            .find(|&at| good[at..at + 2] == 50712u16.to_le_bytes())
            .expect("table");
        let mut bytes = good.clone();
        bytes[table + 2..table + 4].copy_from_slice(&4u16.to_le_bytes());
        bytes[table + 4..table + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Image::parse(bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[table + 4..table + 8].fill(0);
        assert!(matches!(Image::parse(bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[table + 8..table + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Image::parse(bytes), Err(Error::Incomplete)));
        bytes = good;
        let last = u32::from_le_bytes(bytes[table + 8..table + 12].try_into().expect("offset"))
            as usize
            + 2 * u16::MAX as usize;
        bytes[last..last + 2].fill(0);
        let image = Image::parse(bytes).expect("Stage1 still valid");
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut row = [0xa5a5; 2];
        assert_eq!(image.normalized_row(0, &mut row), Err(Error::Unsupported));
        assert_eq!(row, [0xa5a5; 2]);
    }

    fn linearized_mono8_fixture(short: bool) -> Vec<u8> {
        let (mut bytes, _, _) = output_mono_with_tone();
        let old = u32::from_le_bytes(bytes[4..8].try_into().expect("IFD")) as usize;
        let count = u16::from_le_bytes(bytes[old..old + 2].try_into().expect("count")) as usize;
        let mut fields: Vec<[u8; 12]> = bytes[old + 2..old + 2 + count * 12]
            .chunks_exact(12)
            .map(|entry| entry.try_into().expect("tag"))
            .collect();
        let pixels = fields
            .iter()
            .find(|entry| entry[..2] == 273u16.to_le_bytes())
            .expect("pixels");
        let offset = u32::from_le_bytes(pixels[8..12].try_into().expect("offset")) as usize;
        bytes[offset + 3] = 64;
        let value = if short {
            [0u16.to_le_bytes(), u16::from(u8::MAX).to_le_bytes()].concat()
        } else {
            if bytes.len() & 1 != 0 {
                bytes.push(0);
            }
            let table = bytes.len();
            for sample in 0..=u8::MAX {
                bytes
                    .extend_from_slice(&u16::from(sample).saturating_mul(2).min(255).to_le_bytes());
            }
            (table as u32).to_le_bytes().to_vec()
        };
        let tag: [u8; 12] = [
            50712u16.to_le_bytes().as_slice(),
            3u16.to_le_bytes().as_slice(),
            (if short { 2u32 } else { 256 }).to_le_bytes().as_slice(),
            value.as_slice(),
        ]
        .concat()
        .try_into()
        .expect("table tag");
        fields.push(tag);
        fields.sort_by_key(|tag| u16::from_le_bytes(tag[..2].try_into().expect("tag")));
        if bytes.len() & 1 != 0 {
            bytes.push(0);
        }
        let ifd = bytes.len() as u32;
        bytes[4..8].copy_from_slice(&ifd.to_le_bytes());
        bytes.extend_from_slice(&(fields.len() as u16).to_le_bytes());
        for tag in fields {
            bytes.extend_from_slice(&tag);
        }
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes
    }

    #[test]
    fn output_mono8_uses_bounded_table_before_exact_srgb_color() {
        for short in [false, true] {
            let image = Image::parse(linearized_mono8_fixture(short)).expect("valid SDR table");
            assert_eq!(image.stage2_status(), Ok(()));
            assert_eq!(image.stage3_status(), Ok(()));
            assert!(image.supports_final_render());
            let mut raw = [0xa5a5; 3];
            image.raw_row(1, &mut raw[..2]).expect("raw");
            assert_eq!(raw, [255, 64, 0xa5a5]);
            let mapped = if short { u8::MAX } else { 128 };
            let mut stage2 = [0xa5a5; 3];
            image.normalized_row(1, &mut stage2[..2]).expect("Stage2");
            assert_eq!(stage2, [65535, u16::from(mapped) * 257, 0xa5a5]);
            stage2.fill(0xa5a5);
            image.stage3_row(1, &mut stage2[..2]).expect("Stage3");
            assert_eq!(stage2, [65535, u16::from(mapped) * 257, 0xa5a5]);
            let mut rgb = [0xa5; 7];
            assert_eq!(image.copy_rgb_row(1, &mut rgb[..6]), Ok(()));
            assert_eq!(
                rgb,
                [
                    255,
                    255,
                    255,
                    srgb_from_linear_u8(mapped),
                    srgb_from_linear_u8(mapped),
                    srgb_from_linear_u8(mapped),
                    0xa5
                ]
            );
        }
    }

    #[test]
    fn mono8_table_with_out_of_range_values_never_enables_final_output() {
        let mut bytes = linearized_mono8_fixture(false);
        let ifd = u32::from_le_bytes(bytes[4..8].try_into().expect("IFD")) as usize;
        let count = u16::from_le_bytes(bytes[ifd..ifd + 2].try_into().expect("count")) as usize;
        let table = (0..count)
            .map(|i| ifd + 2 + i * 12)
            .find(|&at| bytes[at..at + 2] == 50712u16.to_le_bytes())
            .expect("table");
        let values =
            u32::from_le_bytes(bytes[table + 8..table + 12].try_into().expect("pointer")) as usize;
        bytes[values + 2 * usize::from(u8::MAX)..values + 2 * usize::from(u8::MAX) + 2]
            .copy_from_slice(&256u16.to_le_bytes());
        let image = Image::parse(bytes).expect("raw Stage1 still valid");
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        assert!(!image.supports_final_render());
        let mut row = [0xa5a5; 2];
        assert_eq!(image.normalized_row(1, &mut row), Err(Error::Unsupported));
        assert_eq!(row, [0xa5a5; 2]);
        let mut rgb = [0xa5; 6];
        assert_eq!(image.copy_rgb_row(1, &mut rgb), Err(Error::Unsupported));
        assert_eq!(rgb, [0xa5; 6]);
    }

    #[test]
    fn stage1_multistrip_endian_and_black_white_normalization() {
        for bits in [8, 16] {
            for big_endian in [false, true] {
                let bytes = multi_strip_fixture(big_endian, bits);
                let image = Image::parse(bytes).expect("valid stage-1 DNG");
                assert_eq!(
                    (image.width, image.height, image.bits_per_sample()),
                    (2, 3, bits)
                );
                assert!(!image.supports_final_render());
                let expected = if bits == 8 {
                    [[0, 10], [255, 20], [128, 230]]
                } else {
                    [[0, 1024], [65535, 2048], [32768, 60000]]
                };
                let normalized = if bits == 8 {
                    [[0, 2570], [65535, 5140], [32896, 59110]]
                } else {
                    // Row 2 subtracts 1024 but divides by 60000 - max(1024, 2048).
                    [[0, 0], [65535, 0], [35898, 65535]]
                };
                for (row, raw) in expected.into_iter().enumerate() {
                    let mut samples = [0xffff; 2];
                    image.raw_row(row as u32, &mut samples).expect("raw row");
                    assert_eq!(samples, raw);
                    image
                        .normalized_row(row as u32, &mut samples)
                        .expect("normalized row");
                    assert_eq!(samples, normalized[row]);
                }
                let mut missing = [0u16; 1];
                assert_eq!(image.raw_row(0, &mut missing), Err(Error::Invalid));
                assert_eq!(image.raw_row(3, &mut [0u16; 2]), Err(Error::Invalid));
            }
        }
    }

    #[test]
    fn normalization_rounds_halfway_up_with_max_black_denominator() {
        let mut bytes = multi_strip_fixture(false, 16);
        let offset_entry = 10 + 6 * 12;
        let offset_array = u32::from_le_bytes(
            bytes[offset_entry + 8..offset_entry + 12]
                .try_into()
                .expect("strip offsets pointer"),
        ) as usize;
        let first_strip = u32::from_le_bytes(
            bytes[offset_array..offset_array + 4]
                .try_into()
                .expect("first strip offset"),
        ) as usize;
        // The black level for row 1 is 2048. A sample of 31024 lies exactly
        // halfway after division by the plane's (60000 - 2048) range.
        bytes[first_strip + 6..first_strip + 8].copy_from_slice(&31024u16.to_le_bytes());
        let image = Image::parse(bytes).expect("valid DNG");
        let mut row = [0u16; 2];
        image.normalized_row(1, &mut row).expect("normalized row");
        assert_eq!(row, [65535, 32768]);
    }

    #[test]
    fn maximum_black_compares_rational_values_not_numerators() {
        let mut bytes = multi_strip_fixture(false, 16);
        let black_entry = 10 + 14 * 12;
        let black_data = u32::from_le_bytes(
            bytes[black_entry + 8..black_entry + 12]
                .try_into()
                .expect("black levels pointer"),
        ) as usize;
        // The first repeating level (3000/2) exceeds the second (4000/4),
        // even though its numerator is smaller.
        bytes[black_data..black_data + 8]
            .copy_from_slice(&[3000u32.to_le_bytes(), 2u32.to_le_bytes()].concat());
        bytes[black_data + 8..black_data + 16]
            .copy_from_slice(&[4000u32.to_le_bytes(), 4u32.to_le_bytes()].concat());
        let image = Image::parse(bytes).expect("valid rational black levels");
        let mut row = [0u16; 2];
        image.normalized_row(1, &mut row).expect("normalized row");
        assert_eq!(row, [65535, 1174]);
    }

    #[test]
    fn rejects_partial_strips_and_inconsistent_counts() {
        let bytes = multi_strip_fixture(false, 16);
        assert_eq!(
            Image::parse(bytes[..bytes.len() - 1].to_vec()).err(),
            Some(Error::Incomplete)
        );
        let mut offsets = bytes.clone();
        let offset_entry = 10 + 6 * 12;
        let offset_array = u32::from_le_bytes(
            offsets[offset_entry + 8..offset_entry + 12]
                .try_into()
                .expect("strip offsets pointer"),
        ) as usize;
        offsets[offset_array + 4..offset_array + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(Image::parse(offsets).err(), Some(Error::Incomplete));

        let mut short_count = bytes.clone();
        let sizes_entry = 10 + 9 * 12;
        let sizes_array = u32::from_le_bytes(
            short_count[sizes_entry + 8..sizes_entry + 12]
                .try_into()
                .expect("strip lengths pointer"),
        ) as usize;
        short_count[sizes_array + 4..sizes_array + 8].copy_from_slice(&3u32.to_le_bytes());
        assert_eq!(Image::parse(short_count).err(), Some(Error::Invalid));

        let mut huge_count = bytes.clone();
        huge_count[offset_entry + 4..offset_entry + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            Image::parse(huge_count),
            Err(Error::Invalid | Error::Incomplete)
        ));

        let mut row_count = bytes.clone();
        let rows_per_strip_entry = 10 + 8 * 12;
        row_count[rows_per_strip_entry + 8..rows_per_strip_entry + 12]
            .copy_from_slice(&0u32.to_le_bytes());
        assert_eq!(Image::parse(row_count).err(), Some(Error::Invalid));

        let mut repeat = bytes.clone();
        repeat[10 + 13 * 12 + 8] = 9;
        assert_eq!(Image::parse(repeat).err(), Some(Error::Unsupported));

        let mut black_count = bytes.clone();
        black_count[10 + 14 * 12 + 4..10 + 14 * 12 + 8].copy_from_slice(&1u32.to_le_bytes());
        assert_eq!(Image::parse(black_count).err(), Some(Error::Invalid));

        let mut white = bytes;
        white[10 + 15 * 12 + 8..10 + 15 * 12 + 12].copy_from_slice(&65536u32.to_le_bytes());
        assert_eq!(Image::parse(white).err(), Some(Error::Invalid));
    }
}
