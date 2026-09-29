// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Root classic-TIFF monochrome LinearRaw with checked Deflate strips.

use super::dng::{
    identity_array, number, optional, range, required, scalar, ByteOrder, Error, Tag,
};
use super::linearization::{linearized_sample, LinearizationTable};
use super::tiled::read_ifd;

pub struct Image {
    pixels: Vec<u8>,
    order: ByteOrder,
    width: u32,
    height: u32,
    black_repeat: [u32; 2],
    black: Vec<(u64, u64)>,
    max_black: (u64, u64),
    white: u32,
    linearization: Option<LinearizationTable>,
    stage2_supported: bool,
    stage3_supported: bool,
}

fn field_type(tag: &Tag<'_>) -> Result<(), Error> {
    let valid = match tag.id {
        254 => tag.kind == 4,
        256 | 257 | 273 | 278 | 279 | 50717 | 50829 => matches!(tag.kind, 3 | 4),
        258 | 259 | 262 | 274 | 277 | 284 | 317 | 339 | 50712 | 50713 | 50879 => tag.kind == 3,
        270 | 271 | 272 | 305 | 306 | 315 | 33432 | 50708 => tag.kind == 2,
        50706 | 50707 => tag.kind == 1,
        50718 => tag.kind == 5,
        50714 | 50719 | 50720 => matches!(tag.kind, 3 | 4 | 5),
        50715 | 50716 => tag.kind == 10,
        51009 | 51022 => tag.kind == 7,
        _ => return Err(Error::Unsupported),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

fn black_fraction(tag: &Tag<'_>, index: usize, order: ByteOrder) -> Result<(u64, u64), Error> {
    if index >= tag.count as usize {
        return Err(Error::Invalid);
    }
    if tag.kind != 5 {
        return Ok((u64::from(number(tag, index, order)?), 1));
    }
    let fraction = range(tag.value, index.checked_mul(8).ok_or(Error::Invalid)?, 8)?;
    let denominator = u64::from(order.u32(&fraction[4..]));
    if denominator == 0 {
        return Err(Error::Invalid);
    }
    Ok((u64::from(order.u32(&fraction[..4])), denominator))
}

pub(crate) fn reverse_horizontal_prediction(
    pixels: &mut [u8],
    row_bytes: usize,
    order: ByteOrder,
    channels: usize,
) -> Result<(), Error> {
    if !matches!(channels, 1 | 3)
        || row_bytes < 2 * channels
        || row_bytes % (2 * channels) != 0
        || pixels.len() % row_bytes != 0
    {
        return Err(Error::Invalid);
    }
    for row in pixels.chunks_exact_mut(row_bytes) {
        let mut previous = [0u16; 3];
        for (index, sample) in row.chunks_exact_mut(2).enumerate() {
            let lane = index % channels;
            let current = order.u16(sample).wrapping_add(previous[lane]);
            sample.copy_from_slice(&match order {
                ByteOrder::Little => current.to_le_bytes(),
                ByteOrder::Big => current.to_be_bytes(),
            });
            previous[lane] = current;
        }
    }
    Ok(())
}

impl Image {
    pub fn parse(
        data: &[u8],
        mut validate: impl FnMut(&[u8], usize) -> Result<(), Error>,
        mut decompress: impl FnMut(&[u8], &mut [u8]) -> Result<(), Error>,
    ) -> Result<Option<Self>, Error> {
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
        let entries = range(
            data,
            first.checked_add(2).ok_or(Error::Invalid)?,
            count.checked_mul(12).ok_or(Error::Invalid)?,
        )?;
        let is_deflate = entries.chunks_exact(12).any(|entry| {
            order.u16(&entry[..2]) == 259
                && order.u16(&entry[2..4]) == 3
                && order.u32(&entry[4..8]) == 1
                && order.u16(&entry[8..10]) == 8
        });
        if !is_deflate {
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
        if optional(tags, 330).is_some() {
            return Err(Error::Unsupported);
        }
        for field in tags {
            field_type(field)?;
        }
        if scalar(required(tags, 262)?, order)? != 34892
            || scalar(required(tags, 277)?, order)? != 1
        {
            return Ok(None); // Do not seize RGB or CFA Deflate images.
        }
        let version = required(tags, 50706)?;
        let backward = required(tags, 50707)?;
        if version.count != 4 || backward.count != 4 {
            return Err(Error::Invalid);
        }
        if !matches!(version.value, [1, 4, 0, 0] | [1, 7, 0, 0]) || backward.value != [1, 1, 0, 0] {
            return Err(Error::Unsupported);
        }
        let model = required(tags, 50708)?;
        if model.count < 2 || model.value.last() != Some(&0) {
            return Err(Error::Invalid);
        }
        for id in [270, 271, 272, 305, 306, 315, 33432] {
            if let Some(text) = optional(tags, id) {
                if text.value.last() != Some(&0) {
                    return Err(Error::Invalid);
                }
            }
        }
        let width = scalar(required(tags, 256)?, order)?;
        let height = scalar(required(tags, 257)?, order)?;
        if width == 0 || height == 0 || width > 300_000 || height > 300_000 {
            return Err(Error::Invalid);
        }
        for (id, expected) in [(254, 0), (258, 16)] {
            if let Some(value) = optional(tags, id) {
                if scalar(value, order)? != expected {
                    return Err(Error::Unsupported);
                }
            } else if id == 258 {
                return Err(Error::Invalid);
            }
        }
        for id in [274, 284, 339] {
            if let Some(value) = optional(tags, id) {
                if scalar(value, order)? != 1 {
                    return Err(Error::Unsupported);
                }
            }
        }
        let predictor = optional(tags, 317)
            .map(|tag| scalar(tag, order))
            .transpose()?
            .unwrap_or(1);
        if !matches!(predictor, 1 | 2) {
            return Err(Error::Unsupported);
        }
        if let Some(reference) = optional(tags, 50879) {
            if scalar(reference, order)? > 1 {
                return Err(Error::Unsupported);
            }
        }
        let stage2_geometry_supported = identity_array(tags, 50718, &[1, 1], order).is_ok()
            && identity_array(tags, 50719, &[0, 0], order).is_ok()
            && identity_array(tags, 50720, &[width, height], order).is_ok()
            && identity_array(tags, 50829, &[0, 0, height, width], order).is_ok()
            && [50715, 50716, 51009]
                .into_iter()
                .all(|id| optional(tags, id).is_none());
        let mut repeat = [1, 1];
        if let Some(dim) = optional(tags, 50713) {
            if dim.count != 2 {
                return Err(Error::Invalid);
            }
            for (index, element) in repeat.iter_mut().enumerate() {
                *element = number(dim, index, order)?;
                if *element == 0 || *element > 8 {
                    return Err(Error::Unsupported);
                }
            }
        }
        let black_count = (repeat[0] as usize)
            .checked_mul(repeat[1] as usize)
            .ok_or(Error::Invalid)?;
        let mut black = Vec::new();
        black
            .try_reserve_exact(black_count)
            .map_err(|_| Error::OutOfMemory)?;
        if let Some(levels) = optional(tags, 50714) {
            if levels.count as usize != black_count {
                return Err(Error::Invalid);
            }
            for index in 0..black_count {
                black.push(black_fraction(levels, index, order)?);
            }
        } else if black_count == 1 {
            black.push((0, 1));
        } else {
            return Err(Error::Unsupported);
        }
        let white = if let Some(level) = optional(tags, 50717) {
            scalar(level, order)?
        } else {
            u16::MAX as u32
        };
        if white == 0
            || white > u16::MAX as u32
            || black
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
        let linearization = optional(tags, 50712)
            .map(|tag| LinearizationTable::parse(tag, order))
            .transpose()?;
        let stage2_supported = stage2_geometry_supported
            && linearization.as_ref().is_none_or(|table| {
                white == u32::from(u16::MAX)
                    && black.iter().all(|&(numerator, _)| numerator == 0)
                    && table.has_identity_endpoints()
            });

        let rows_per_strip = scalar(required(tags, 278)?, order)?;
        if rows_per_strip == 0 {
            return Err(Error::Invalid);
        }
        let count = 1 + (height - 1) / rows_per_strip;
        let offsets = required(tags, 273)?;
        let sizes = required(tags, 279)?;
        if offsets.count != count || sizes.count != count {
            return Err(Error::Invalid);
        }
        let row_bytes = (width as usize).checked_mul(2).ok_or(Error::Invalid)?;
        let total_bytes = (height as usize)
            .checked_mul(row_bytes)
            .ok_or(Error::Invalid)?;
        for index in 0..count {
            let size = number(sizes, index as usize, order)? as usize;
            if size == 0 {
                return Err(Error::Invalid);
            }
            range(data, number(offsets, index as usize, order)? as usize, size)?;
        }
        for index in 0..count {
            let start_row = index.checked_mul(rows_per_strip).ok_or(Error::Invalid)?;
            let rows = (height - start_row).min(rows_per_strip);
            let expected_size = (rows as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            let compressed = range(
                data,
                number(offsets, index as usize, order)? as usize,
                number(sizes, index as usize, order)? as usize,
            )?;
            validate(compressed, expected_size)?;
        }
        let mut pixels = Vec::new();
        pixels
            .try_reserve_exact(total_bytes)
            .map_err(|_| Error::OutOfMemory)?;
        pixels.resize(total_bytes, 0);
        for index in 0..count {
            let start_row = index.checked_mul(rows_per_strip).ok_or(Error::Invalid)?;
            let rows = (height - start_row).min(rows_per_strip);
            let start = (start_row as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            let size = (rows as usize)
                .checked_mul(row_bytes)
                .ok_or(Error::Invalid)?;
            let compressed = range(
                data,
                number(offsets, index as usize, order)? as usize,
                number(sizes, index as usize, order)? as usize,
            )?;
            let dest = pixels
                .get_mut(start..start.checked_add(size).ok_or(Error::Invalid)?)
                .ok_or(Error::Invalid)?;
            decompress(compressed, dest)?;
            if predictor == 2 {
                reverse_horizontal_prediction(dest, row_bytes, order, 1)?;
            }
        }
        Ok(Some(Self {
            pixels,
            order,
            width,
            height,
            black_repeat: repeat,
            black,
            max_black,
            white,
            linearization,
            stage2_supported,
            stage3_supported: stage2_supported && optional(tags, 51022).is_none(),
        }))
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
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

    pub fn raw_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        if row >= self.height || output.len() != self.width as usize {
            return Err(Error::Invalid);
        }
        let start = (row as usize)
            .checked_mul(self.width as usize)
            .and_then(|start| start.checked_mul(2))
            .ok_or(Error::Invalid)?;
        let bytes = range(
            &self.pixels,
            start,
            output.len().checked_mul(2).ok_or(Error::Invalid)?,
        )?;
        for (source, dest) in bytes.chunks_exact(2).zip(output.iter_mut()) {
            *dest = self.order.u16(source);
        }
        Ok(())
    }

    pub fn normalized_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage2_status()?;
        self.raw_row(row, output)?;
        let (max_num, max_den) = self.max_black;
        for (column, sample) in output.iter_mut().enumerate() {
            let black_index = ((row % self.black_repeat[0]) * self.black_repeat[1]
                + (column as u32 % self.black_repeat[1])) as usize;
            let (numerator, denominator) = self.black[black_index];
            let scaled_sample = u128::from(linearized_sample(self.linearization.as_ref(), *sample))
                * u128::from(denominator);
            if scaled_sample <= u128::from(numerator) {
                *sample = 0;
                continue;
            }
            let span = u128::from(self.white)
                .checked_mul(u128::from(max_den))
                .and_then(|value| value.checked_sub(u128::from(max_num)))
                .and_then(|value| value.checked_mul(u128::from(denominator)))
                .ok_or(Error::Invalid)?;
            let scaled = (scaled_sample - u128::from(numerator))
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
}

#[cfg(test)]
mod tests {
    use super::{reverse_horizontal_prediction, ByteOrder, Error, Image};
    use std::cell::Cell;

    const RAW: [u16; 9] = [0, 1024, 65535, 2048, 32768, 60000, 800, 50000, 12345];
    const SDK_STAGE2: [u16; 9] = [0, 0, 65535, 0, 34740, 65535, 0, 55384, 12802];

    fn fixture(big_endian: bool, rows_per_strip: u32, version: u8) -> Vec<u8> {
        fixture_with_predictor(big_endian, rows_per_strip, version, 1)
    }

    fn fixture_with_predictor(
        big_endian: bool,
        rows_per_strip: u32,
        version: u8,
        predictor: u16,
    ) -> Vec<u8> {
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
        let strips = 1 + (3 - 1) / rows_per_strip;
        let mut fields = vec![
            (254, 4, 1, long(0).to_vec()),
            (256, 4, 1, long(3).to_vec()),
            (257, 4, 1, long(3).to_vec()),
            (258, 3, 1, word(16).to_vec()),
            (259, 3, 1, word(8).to_vec()),
            (262, 3, 1, word(34892).to_vec()),
            (273, 4, strips, vec![0; strips as usize * 4]),
            (274, 3, 1, word(1).to_vec()),
            (277, 3, 1, word(1).to_vec()),
            (278, 4, 1, long(rows_per_strip).to_vec()),
            (279, 4, strips, (0..strips).flat_map(|_| long(2)).collect()),
            (284, 3, 1, word(1).to_vec()),
            (317, 3, 1, word(predictor).to_vec()),
            (339, 3, 1, word(1).to_vec()),
            (50706, 1, 4, vec![1, version, 0, 0]),
            (50707, 1, 4, vec![1, 1, 0, 0]),
            (50708, 2, 5, b"mono\0".to_vec()),
            (50713, 3, 2, [word(2), word(1)].concat()),
            (50714, 5, 2, pair(1024, 2048)),
            (50717, 4, 1, long(60000).to_vec()),
            (50718, 5, 2, pair(1, 1)),
            (50719, 5, 2, pair(0, 0)),
            (50720, 5, 2, pair(3, 3)),
            (50829, 4, 4, [long(0), long(0), long(3), long(3)].concat()),
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
        let mut offsets = 0usize;
        for (index, (id, kind, count, value)) in fields.iter().enumerate() {
            let at = table + 12 * index;
            bytes[at..at + 2].copy_from_slice(&word(*id));
            bytes[at + 2..at + 4].copy_from_slice(&word(*kind));
            bytes[at + 4..at + 8].copy_from_slice(&long(*count));
            if value.len() <= 4 {
                bytes[at + 8..at + 8 + value.len()].copy_from_slice(value);
                if *id == 273 {
                    offsets = at + 8;
                }
            } else {
                if bytes.len() % 2 != 0 {
                    bytes.push(0);
                }
                let location = bytes.len();
                bytes[at + 8..at + 12].copy_from_slice(&long(location as u32));
                bytes.extend_from_slice(value);
                if *id == 273 {
                    offsets = location;
                }
            }
        }
        for strip in 0..strips {
            if bytes.len() % 2 != 0 {
                bytes.push(0);
            }
            let position = long(bytes.len() as u32);
            bytes[offsets + strip as usize * 4..offsets + strip as usize * 4 + 4]
                .copy_from_slice(&position);
            bytes.extend_from_slice(&[8, strip as u8]);
        }
        bytes
    }

    fn field(bytes: &[u8], id: u16, big_endian: bool) -> usize {
        let read = |data: &[u8]| {
            if big_endian {
                u16::from_be_bytes(data.try_into().expect("field"))
            } else {
                u16::from_le_bytes(data.try_into().expect("field"))
            }
        };
        let count = read(&bytes[8..10]) as usize;
        (0..count)
            .map(|index| 10 + 12 * index)
            .find(|&at| read(&bytes[at..at + 2]) == id)
            .expect("field id")
    }

    fn linearized_fixture(big_endian: bool, predictor: u16) -> Vec<u8> {
        let mut bytes = fixture_with_predictor(big_endian, 1, 4, predictor);
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
        let offset = |bytes: &[u8], at: usize| {
            (if big_endian {
                u32::from_be_bytes(bytes[at..at + 4].try_into().expect("offset"))
            } else {
                u32::from_le_bytes(bytes[at..at + 4].try_into().expect("offset"))
            }) as usize
        };
        let format = field(&bytes, 339, big_endian);
        bytes[format..format + 12].copy_from_slice(
            &[
                word(50712).as_slice(),
                word(3).as_slice(),
                long(2).as_slice(),
                word(0).as_slice(),
                word(u16::MAX).as_slice(),
            ]
            .concat(),
        );
        let black = field(&bytes, 50714, big_endian);
        let values = offset(&bytes, black + 8);
        for at in [values, values + 8] {
            bytes[at..at + 4].copy_from_slice(&long(0));
        }
        let white = field(&bytes, 50717, big_endian);
        bytes[white + 8..white + 12].copy_from_slice(&long(65535));
        bytes
    }

    #[test]
    fn mono_deflate_linearization_maps_after_predictor_without_changing_raw_rows() {
        for big_endian in [false, true] {
            for predictor in [1, 2] {
                let bytes = linearized_fixture(big_endian, predictor);
                let validated = Cell::new(0);
                let inflated = Cell::new(0);
                let image = Image::parse(
                    &bytes,
                    |encoded, expected| {
                        assert_eq!((encoded.len(), expected), (2, 6));
                        validated.set(validated.get() + 1);
                        Ok(())
                    },
                    |_encoded, output| {
                        assert_eq!(validated.get(), 3);
                        let first = inflated.get() * 3;
                        for (index, dest) in output.chunks_exact_mut(2).enumerate() {
                            let value = RAW[first + index];
                            let previous = if predictor == 2 && index > 0 {
                                RAW[first + index - 1]
                            } else {
                                0
                            };
                            dest.copy_from_slice(&if big_endian {
                                value.wrapping_sub(previous).to_be_bytes()
                            } else {
                                value.wrapping_sub(previous).to_le_bytes()
                            });
                        }
                        inflated.set(inflated.get() + 1);
                        Ok(())
                    },
                )
                .expect("valid Deflate linearization")
                .expect("mono route");
                assert_eq!((validated.get(), inflated.get()), (3, 3));
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Ok(()));
                for y in 0..3 {
                    let mut raw = [0xa5a5; 4];
                    let mut mapped = [0xa5a5; 4];
                    image.raw_row(y, &mut raw[..3]).expect("Stage1");
                    image.normalized_row(y, &mut mapped[..3]).expect("Stage2");
                    assert_eq!(raw[..3], RAW[y as usize * 3..(y + 1) as usize * 3]);
                    for i in 0..3 {
                        assert_eq!(mapped[i], if raw[i] == 0 { 0 } else { u16::MAX });
                    }
                    assert_eq!((raw[3], mapped[3]), (0xa5a5, 0xa5a5));
                    let expected = mapped;
                    mapped.fill(0xa5a5);
                    image.stage3_row(y, &mut mapped[..3]).expect("Stage3");
                    assert_eq!(mapped[..3], expected[..3]);
                }
            }
        }
    }

    #[test]
    fn mono_deflate_strips_decode_to_exact_sdk_stage_rows() {
        for big_endian in [false, true] {
            for rows in [1, 3] {
                for version in [4, 7] {
                    let bytes = fixture(big_endian, rows, version);
                    let decoded = Cell::new(0);
                    let validated = Cell::new(0);
                    let image = Image::parse(
                        &bytes,
                        |encoded, expected| {
                            assert_eq!(encoded[0], 8);
                            assert_eq!(expected, rows.min(3) as usize * 3 * 2);
                            validated.set(validated.get() + 1);
                            Ok(())
                        },
                        |encoded, output| {
                            assert_eq!(encoded[0], 8);
                            let first = usize::from(encoded[1]) * rows as usize * 3;
                            for (sample, raw) in RAW[first..first + output.len() / 2]
                                .iter()
                                .zip(output.chunks_exact_mut(2))
                            {
                                let value = if big_endian {
                                    sample.to_be_bytes()
                                } else {
                                    sample.to_le_bytes()
                                };
                                raw.copy_from_slice(&value);
                            }
                            decoded.set(decoded.get() + 1);
                            Ok(())
                        },
                    )
                    .expect("valid Deflate metadata")
                    .expect("Deflate mono path");
                    assert_eq!(validated.get(), (1 + (3 - 1) / rows) as usize);
                    assert_eq!(decoded.get(), (1 + (3 - 1) / rows) as usize);
                    assert_eq!((image.width(), image.height()), (3, 3));
                    assert_eq!(image.stage2_status(), Ok(()));
                    assert_eq!(image.stage3_status(), Ok(()));
                    for repeat in 0..2 {
                        for y in 0..3 {
                            let mut raw = [0xa5a5; 4];
                            let mut stage2 = [0xa5a5; 4];
                            let mut stage3 = [0xa5a5; 4];
                            image.raw_row(y, &mut raw[..3]).expect("Stage 1");
                            image.normalized_row(y, &mut stage2[..3]).expect("Stage 2");
                            image.stage3_row(y, &mut stage3[..3]).expect("Stage 3");
                            assert_eq!(
                                raw[..3],
                                RAW[y as usize * 3..(y + 1) as usize * 3],
                                "repeat {repeat}"
                            );
                            assert_eq!(
                                stage2[..3],
                                SDK_STAGE2[y as usize * 3..(y + 1) as usize * 3]
                            );
                            assert_eq!(stage3[..3], stage2[..3]);
                            assert_eq!((raw[3], stage2[3], stage3[3]), (0xa5a5, 0xa5a5, 0xa5a5));
                        }
                    }
                    let mut sentinel = [0xa5a5; 2];
                    assert_eq!(image.raw_row(0, &mut sentinel), Err(Error::Invalid));
                    assert_eq!(sentinel, [0xa5a5; 2]);
                }
            }
        }
    }

    #[test]
    fn mono_deflate_predictor2_restarts_at_each_row_and_wraps() {
        for big_endian in [false, true] {
            for rows in [1, 3] {
                let bytes = fixture_with_predictor(big_endian, rows, 4, 2);
                let image = Image::parse(
                    &bytes,
                    |_, expected| {
                        assert_eq!(expected % 6, 0);
                        Ok(())
                    },
                    |encoded, output| {
                        let start = usize::from(encoded[1]) * rows as usize * 3;
                        for (index, bytes) in output.chunks_exact_mut(2).enumerate() {
                            let pixel = start + index;
                            let left = if pixel % 3 == 0 { 0 } else { RAW[pixel - 1] };
                            let value = RAW[pixel].wrapping_sub(left);
                            bytes.copy_from_slice(&if big_endian {
                                value.to_be_bytes()
                            } else {
                                value.to_le_bytes()
                            });
                        }
                        Ok(())
                    },
                )
                .expect("valid predictor metadata")
                .expect("Deflate predictor 2");
                assert_eq!(image.stage3_status(), Ok(()));
                for y in 0..3 {
                    let mut stage1 = [0; 3];
                    let mut stage2 = [0; 3];
                    let mut stage3 = [0; 3];
                    image.raw_row(y, &mut stage1).expect("Stage 1");
                    image.normalized_row(y, &mut stage2).expect("Stage 2");
                    image.stage3_row(y, &mut stage3).expect("Stage 3");
                    assert_eq!(stage1, RAW[y as usize * 3..(y + 1) as usize * 3]);
                    assert_eq!(stage2, SDK_STAGE2[y as usize * 3..(y + 1) as usize * 3]);
                    assert_eq!(stage3, stage2);
                }
            }
        }
    }

    #[test]
    fn rgb_predictor2_uses_previous_pixel_per_channel_and_resets_rows() {
        const RGB: [u16; 18] = [
            0, 1, 65535, 256, 257, 258, 32768, 60000, 0, 65535, 0, 12345, 42, 43, 44, 50000, 60000,
            65534,
        ];
        for order in [ByteOrder::Little, ByteOrder::Big] {
            let mut encoded = Vec::new();
            for row in RGB.chunks_exact(9) {
                for (index, &sample) in row.iter().enumerate() {
                    let previous = if index < 3 { 0 } else { row[index - 3] };
                    let difference = sample.wrapping_sub(previous);
                    encoded.extend_from_slice(&match order {
                        ByteOrder::Little => difference.to_le_bytes(),
                        ByteOrder::Big => difference.to_be_bytes(),
                    });
                }
            }
            let mut truncated = encoded[..encoded.len() - 1].to_vec();
            let before = truncated.clone();
            assert_eq!(
                reverse_horizontal_prediction(&mut truncated, 18, order, 3),
                Err(Error::Invalid)
            );
            assert_eq!(truncated, before);
            reverse_horizontal_prediction(&mut encoded, 18, order, 3)
                .expect("valid RGB prediction");
            let samples: Vec<_> = encoded
                .chunks_exact(2)
                .map(|bytes| order.u16(bytes))
                .collect();
            assert_eq!(samples, RGB);
        }
    }

    #[test]
    fn malformed_strips_and_processing_fail_without_publishing_pixels() {
        let good = fixture(false, 1, 4);
        let offset_field = field(&good, 273, false);
        let offset = u32::from_le_bytes(
            good[offset_field + 8..offset_field + 12]
                .try_into()
                .expect("offset array"),
        ) as usize;
        let mut invalid = good.clone();
        invalid[offset + 4..offset + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        let calls = Cell::new(0);
        assert!(matches!(
            Image::parse(
                &invalid,
                |_, _| {
                    calls.set(calls.get() + 1);
                    Ok(())
                },
                |_, _| Ok(())
            ),
            Err(Error::Incomplete)
        ));
        assert_eq!(calls.get(), 0);
        let validated = Cell::new(0);
        let decoded = Cell::new(0);
        assert!(matches!(
            Image::parse(
                &good,
                |encoded, expected| {
                    assert_eq!(expected, 6);
                    validated.set(validated.get() + 1);
                    if encoded[1] == 2 {
                        Err(Error::Invalid)
                    } else {
                        Ok(())
                    }
                },
                |_, _| {
                    decoded.set(decoded.get() + 1);
                    Ok(())
                },
            ),
            Err(Error::Invalid)
        ));
        assert_eq!((validated.get(), decoded.get()), (3, 0));
        let mut count = good.clone();
        count[offset_field + 4..offset_field + 8].copy_from_slice(&2u32.to_le_bytes());
        assert!(matches!(
            Image::parse(&count, |_, _| Ok(()), |_, _| Ok(())),
            Err(Error::Invalid)
        ));
        let mut wrong_type = good.clone();
        wrong_type[offset_field + 2..offset_field + 4].copy_from_slice(&1u16.to_le_bytes());
        assert!(matches!(
            Image::parse(&wrong_type, |_, _| Ok(()), |_, _| Ok(())),
            Err(Error::Invalid)
        ));
        assert!(matches!(
            Image::parse(&good, |_, _| Err(Error::Incomplete), |_, _| Ok(())),
            Err(Error::Incomplete)
        ));
        assert!(matches!(
            Image::parse(&good, |_, _| Ok(()), |_, _| Err(Error::Incomplete)),
            Err(Error::Incomplete)
        ));
        let mut predictor = good.clone();
        let at = field(&predictor, 317, false);
        predictor[at + 8..at + 10].copy_from_slice(&3u16.to_le_bytes());
        assert!(matches!(
            Image::parse(&predictor, |_, _| Ok(()), |_, _| Ok(())),
            Err(Error::Unsupported)
        ));
        let mut crop = good.clone();
        let at = field(&crop, 50719, false);
        let value =
            u32::from_le_bytes(crop[at + 8..at + 12].try_into().expect("crop pointer")) as usize;
        crop[value..value + 4].copy_from_slice(&1u32.to_le_bytes());
        let image = Image::parse(
            &crop,
            |_, _| Ok(()),
            |_encoded, output| {
                output.fill(0);
                Ok(())
            },
        )
        .expect("Stage 1")
        .expect("Deflate mono");
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut untouched = [0xa5a5; 3];
        assert_eq!(
            image.normalized_row(0, &mut untouched),
            Err(Error::Unsupported)
        );
        assert_eq!(untouched, [0xa5a5; 3]);
        assert_eq!(image.stage3_row(0, &mut untouched), Err(Error::Unsupported));
        assert_eq!(untouched, [0xa5a5; 3]);

        let mut opcode3 = good;
        let at = field(&opcode3, 317, false);
        opcode3[at..at + 2].copy_from_slice(&51022u16.to_le_bytes());
        opcode3[at + 2..at + 4].copy_from_slice(&7u16.to_le_bytes());
        opcode3[at + 4..at + 8].copy_from_slice(&4u32.to_le_bytes());
        opcode3[at + 8..at + 12].fill(0);
        let image = Image::parse(
            &opcode3,
            |_, _| Ok(()),
            |_, output| {
                output.fill(0);
                Ok(())
            },
        )
        .expect("Stage 1")
        .expect("Deflate mono");
        assert_eq!(image.stage2_status(), Ok(()));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_row(0, &mut untouched), Err(Error::Unsupported));
        assert_eq!(untouched, [0xa5a5; 3]);
    }
}
