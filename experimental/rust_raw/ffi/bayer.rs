// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Narrow classic-TIFF DNG RGGB sensor path: uncompressed strips or checked
//! Deflate strips or 16-bit SOF3 tiles at Stage 1, with black/white
//! normalization and checked linearization at Stage 2.

use super::deflate::reverse_horizontal_prediction;
use super::dng::{
    identity_array, number, optional, range, required, scalar, ByteOrder, Error, Tag,
};
use super::linearization::{linearized_sample, LinearizationTable};
use super::tiled::read_ifd;

#[path = "demosaic.rs"]
mod demosaic;

struct Strip {
    offset: usize,
    size: usize,
}

struct Tile {
    offset: usize,
    size: usize,
    x: usize,
    y: usize,
    visible_width: usize,
    visible_height: usize,
}

pub struct Plan {
    order: ByteOrder,
    width: u32,
    height: u32,
    compression: u16,
    predictor: u16,
    rows_per_strip: u32,
    strips: Vec<Strip>,
    tiles: Vec<Tile>,
    tile_width: u32,
    tile_height: u32,
    linearization: Option<LinearizationTable>,
    black: [(u64, u64); 4],
    max_black: (u64, u64),
    white: u32,
    stage2_supported: bool,
    stage3_supported: bool,
    stage3_constant: Option<[u16; 3]>,
}

pub struct Image {
    bytes: Vec<u8>,
    plan: Plan,
    pixels: Option<Vec<u16>>,
}

fn tag_type(tag: &Tag<'_>) -> Result<(), Error> {
    let kind = tag.kind;
    let valid = match tag.id {
        254 | 50941 | 51110 => kind == 4,
        256 | 257 | 273 | 278 | 279 | 322 | 323 | 324 | 325 | 50717 | 50829 => {
            kind == 3 || kind == 4
        }
        258 | 259 | 262 | 274 | 277 | 284 | 317 | 339 | 33421 | 50711 | 50712 | 50713 | 50778
        | 50879 => kind == 3,
        33422 | 50710 => kind == 1,
        270 | 271 | 272 | 305 | 306 | 50708 | 50936 => kind == 2,
        50706 | 50707 => kind == 1,
        50714 | 50719 | 50720 => matches!(kind, 3 | 4 | 5),
        50718 | 50727 | 50728 | 50731 | 50732 | 50734 | 50738 | 50739 | 50780 => kind == 5,
        50721 | 50722 | 50730 | 50715 | 50716 | 50964 => kind == 10,
        51009 | 51022 => kind == 7,
        _ => return Err(Error::Unsupported),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

fn fraction(tag: &Tag<'_>, index: usize, order: ByteOrder) -> Result<(u64, u64), Error> {
    if index >= tag.count as usize {
        return Err(Error::Invalid);
    }
    if tag.kind != 5 {
        return Ok((u64::from(number(tag, index, order)?), 1));
    }
    let bytes = range(tag.value, index.checked_mul(8).ok_or(Error::Invalid)?, 8)?;
    let numerator = u64::from(order.u32(&bytes[..4]));
    let denominator = u64::from(order.u32(&bytes[4..]));
    if denominator == 0 {
        return Err(Error::Invalid);
    }
    Ok((numerator, denominator))
}

fn constant_field(
    data: &[u8],
    order: ByteOrder,
    width: u32,
    height: u32,
    rows_per_strip: u32,
    strips: &[Strip],
) -> Result<Option<[u16; 3]>, Error> {
    let row_bytes = (width as usize).checked_mul(2).ok_or(Error::Invalid)?;
    let mut channels = [None; 3];
    for y in 0..height {
        let strip = strips
            .get((y / rows_per_strip) as usize)
            .ok_or(Error::Invalid)?;
        let at = ((y % rows_per_strip) as usize)
            .checked_mul(row_bytes)
            .and_then(|n| strip.offset.checked_add(n))
            .ok_or(Error::Invalid)?;
        let row = range(data, at, row_bytes)?;
        for (x, bytes) in row.chunks_exact(2).enumerate() {
            if !match_constant_sample(&mut channels, x, y as usize, order.u16(bytes)) {
                return Ok(None);
            }
        }
    }
    match channels {
        [Some(r), Some(g), Some(b)] => Ok(Some([r, g, b])),
        _ => Ok(None),
    }
}

fn match_constant_sample(channels: &mut [Option<u16>; 3], x: usize, y: usize, sample: u16) -> bool {
    let channel = match (y & 1, x & 1) {
        (0, 0) => 0,
        (1, 1) => 2,
        _ => 1,
    };
    match channels[channel] {
        Some(previous) => previous == sample,
        None => {
            channels[channel] = Some(sample);
            true
        }
    }
}

fn constant_pixels(pixels: &[u16], width: u32, height: u32) -> Result<Option<[u16; 3]>, Error> {
    let stride = width as usize;
    if stride == 0 || pixels.len() != stride.checked_mul(height as usize).ok_or(Error::Invalid)? {
        return Err(Error::Invalid);
    }
    let mut channels = [None; 3];
    for (y, row) in pixels.chunks_exact(stride).enumerate() {
        for (x, &sample) in row.iter().enumerate() {
            if !match_constant_sample(&mut channels, x, y, sample) {
                return Ok(None);
            }
        }
    }
    match channels {
        [Some(r), Some(g), Some(b)] => Ok(Some([r, g, b])),
        _ => Ok(None),
    }
}

impl Plan {
    pub fn parse(data: &[u8]) -> Result<Option<Self>, Error> {
        Self::parse_storage(data, 1)
    }

    pub fn parse_deflate(data: &[u8]) -> Result<Option<Self>, Error> {
        Self::parse_storage(data, 8)
    }

    fn parse_storage(data: &[u8], expected_compression: u16) -> Result<Option<Self>, Error> {
        let tiled = expected_compression == 7;
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
            return Ok(None); // This sensor slice does not select raw SubIFDs.
        }
        let looks_bayer = entries.chunks_exact(12).any(|entry| {
            order.u16(&entry[..2]) == 262
                && order.u16(&entry[2..4]) == 3
                && order.u32(&entry[4..8]) == 1
                && order.u16(&entry[8..10]) == 32803
        });
        if !looks_bayer {
            return Ok(None); // Do not seize monochrome LinearRaw.
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
            return Err(Error::Unsupported); // OpcodeList1 changes Stage 1.
        }
        let tags = &ifd.tags;
        for tag in tags {
            tag_type(tag)?;
        }
        let version = required(tags, 50706)?;
        let backward = required(tags, 50707)?;
        if version.count != 4 || backward.count != 4 {
            return Err(Error::Invalid);
        }
        if version.value != &[1, 4, 0, 0] || backward.value != &[1, 1, 0, 0] {
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
        let compression = scalar(required(tags, 259)?, order)?;
        if compression != u32::from(expected_compression) {
            return if matches!(compression, 1 | 7 | 8) {
                Ok(None)
            } else {
                Err(Error::Unsupported)
            };
        }
        let predictor = optional(tags, 317)
            .map(|tag| scalar(tag, order))
            .transpose()?
            .unwrap_or(1);
        if predictor != 1 && !(compression == 8 && predictor == 2)
            || tiled && optional(tags, 317).is_some()
        {
            return Err(Error::Unsupported);
        }
        for (id, expected) in [(254, 0), (258, 16), (262, 32803), (277, 1), (284, 1)] {
            if scalar(required(tags, id)?, order)? != expected {
                return Err(Error::Unsupported);
            }
        }
        if let Some(sample_format) = optional(tags, 339) {
            if scalar(sample_format, order)? != 1 {
                return Err(Error::Unsupported);
            }
        }
        let repeating = required(tags, 33421)?;
        let pattern = required(tags, 33422)?;
        let planes = required(tags, 50710)?;
        if repeating.count != 2
            || number(repeating, 0, order)? != 2
            || number(repeating, 1, order)? != 2
            || pattern.count != 4
            || pattern.value != &[0, 1, 1, 2]
            || planes.count != 3
            || planes.value != &[0, 1, 2]
        {
            return Err(Error::Unsupported);
        }
        if scalar(required(tags, 50711)?, order)? != 1 {
            return Err(Error::Unsupported);
        }
        let linearization = optional(tags, 50712)
            .map(|tag| LinearizationTable::parse(tag, order))
            .transpose()?;
        let repeat_black = required(tags, 50713)?;
        let levels = required(tags, 50714)?;
        if repeat_black.count != 2
            || number(repeat_black, 0, order)? != 2
            || number(repeat_black, 1, order)? != 2
            || levels.count != 4
        {
            return Err(Error::Unsupported);
        }
        let mut black = [(0u64, 1u64); 4];
        for (i, level) in black.iter_mut().enumerate() {
            *level = fraction(levels, i, order)?;
        }
        let white = scalar(required(tags, 50717)?, order)?;
        if white == 0 || white > 65535 || black.iter().any(|&(n, d)| n >= u64::from(white) * d) {
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
        let stage2_supported = optional(tags, 274).is_none_or(|tag| scalar(tag, order) == Ok(1))
            && (compression != 8
                || white == 65535 && black.iter().all(|&(numerator, _)| numerator == 0))
            && linearization.as_ref().is_none_or(|table| {
                white == 65535
                    && black.iter().all(|&(numerator, _)| numerator == 0)
                    && table.has_identity_endpoints()
            })
            && optional(tags, 50718)
                .is_some_and(|_| identity_array(tags, 50718, &[1, 1], order).is_ok())
            && optional(tags, 50719)
                .is_some_and(|_| identity_array(tags, 50719, &[0, 0], order).is_ok())
            && optional(tags, 50720)
                .is_some_and(|_| identity_array(tags, 50720, &[width, height], order).is_ok())
            && identity_array(tags, 50829, &[0, 0, height, width], order).is_ok()
            && [50715, 50716, 51009]
                .into_iter()
                .all(|id| optional(tags, id).is_none())
            && (!tiled
                || [50734, 50738, 50739, 50780, 50941]
                    .into_iter()
                    .all(|id| optional(tags, id).is_none()));

        let mut strips = Vec::new();
        let mut tiles = Vec::new();
        let mut rows_per_strip = 0;
        let mut tile_width = 0;
        let mut tile_height = 0;
        if tiled {
            if [273, 278, 279]
                .into_iter()
                .any(|id| optional(tags, id).is_some())
            {
                return Err(Error::Unsupported);
            }
            tile_width = scalar(required(tags, 322)?, order)?;
            tile_height = scalar(required(tags, 323)?, order)?;
            if tile_width == 0 || tile_height == 0 {
                return Err(Error::Invalid);
            }
            if tile_width > 65535 || tile_height > 65535 {
                return Err(Error::Unsupported);
            }
            let columns = 1 + (width - 1) / tile_width;
            let rows = 1 + (height - 1) / tile_height;
            let count = columns.checked_mul(rows).ok_or(Error::Invalid)?;
            let offsets = required(tags, 324)?;
            let lengths = required(tags, 325)?;
            if offsets.count != count || lengths.count != count {
                return Err(Error::Invalid);
            }
            let output_len = (width as usize)
                .checked_mul(height as usize)
                .ok_or(Error::Invalid)?;
            for index in 0..count as usize {
                let offset = number(offsets, index, order)? as usize;
                let size = number(lengths, index, order)? as usize;
                if size == 0 {
                    return Err(Error::Invalid);
                }
                range(data, offset, size)?;
            }
            tiles
                .try_reserve_exact(count as usize)
                .map_err(|_| Error::OutOfMemory)?;
            for index in 0..count {
                let x = (index % columns)
                    .checked_mul(tile_width)
                    .ok_or(Error::Invalid)? as usize;
                let y = (index / columns)
                    .checked_mul(tile_height)
                    .ok_or(Error::Invalid)? as usize;
                let visible_width = (width as usize - x).min(tile_width as usize);
                let visible_height = (height as usize - y).min(tile_height as usize);
                let end = y
                    .checked_add(visible_height - 1)
                    .and_then(|last| last.checked_mul(width as usize))
                    .and_then(|last| last.checked_add(x))
                    .and_then(|last| last.checked_add(visible_width))
                    .ok_or(Error::Invalid)?;
                if end > output_len {
                    return Err(Error::Invalid);
                }
                tiles.push(Tile {
                    offset: number(offsets, index as usize, order)? as usize,
                    size: number(lengths, index as usize, order)? as usize,
                    x,
                    y,
                    visible_width,
                    visible_height,
                });
            }
        } else {
            if [322, 323, 324, 325]
                .into_iter()
                .any(|id| optional(tags, id).is_some())
            {
                return Err(Error::Unsupported);
            }
            rows_per_strip = scalar(required(tags, 278)?, order)?;
            if rows_per_strip == 0 {
                return Err(Error::Invalid);
            }
            let strip_count = 1 + (height - 1) / rows_per_strip;
            let offsets = required(tags, 273)?;
            let lengths = required(tags, 279)?;
            if offsets.count != strip_count || lengths.count != strip_count {
                return Err(Error::Invalid);
            }
            let row_bytes = (width as usize).checked_mul(2).ok_or(Error::Invalid)?;
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
                range(data, number(offsets, index as usize, order)? as usize, size)?;
            }
            strips
                .try_reserve_exact(strip_count as usize)
                .map_err(|_| Error::OutOfMemory)?;
            for index in 0..strip_count {
                strips.push(Strip {
                    offset: number(offsets, index as usize, order)? as usize,
                    size: number(lengths, index as usize, order)? as usize,
                });
            }
        }
        let stage3_supported = stage2_supported
            && width >= 2
            && height >= 2
            && white == 65535
            && black.iter().all(|&(numerator, _)| numerator == 0)
            && [50734, 50738, 50739, 50780, 50941, 51022]
                .into_iter()
                .all(|id| optional(tags, id).is_none());
        let stage3_constant = if compression == 1 && stage3_supported {
            constant_field(data, order, width, height, rows_per_strip, &strips)?
                .map(|rgb| rgb.map(|sample| linearized_sample(linearization.as_ref(), sample)))
        } else {
            None
        };
        Ok(Some(Self {
            order,
            width,
            height,
            compression: compression as u16,
            predictor: predictor as u16,
            rows_per_strip,
            strips,
            tiles,
            tile_width,
            tile_height,
            linearization,
            black,
            max_black,
            white,
            stage2_supported,
            stage3_supported,
            stage3_constant,
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

    pub fn parse_tiled(
        data: &[u8],
        mut validate: impl FnMut(&[u8], u32, u32) -> Result<(), Error>,
        mut decode: impl FnMut(&[u8], u32, u32, u32, u32, &mut [u16]) -> Result<(), Error>,
    ) -> Result<Option<Self>, Error> {
        let Some(mut plan) = Plan::parse_storage(data, 7)? else {
            return Ok(None);
        };
        for tile in &plan.tiles {
            validate(
                range(data, tile.offset, tile.size)?,
                plan.tile_width,
                plan.tile_height,
            )?;
        }
        let count = (plan.width as usize)
            .checked_mul(plan.height as usize)
            .ok_or(Error::Invalid)?;
        let mut pixels = Vec::new();
        pixels
            .try_reserve_exact(count)
            .map_err(|_| Error::OutOfMemory)?;
        pixels.resize(count, 0u16);
        let mut buffer = Vec::new();
        for tile in &plan.tiles {
            let visible_len = tile
                .visible_width
                .checked_mul(tile.visible_height)
                .ok_or(Error::Invalid)?;
            if buffer.len() < visible_len {
                buffer
                    .try_reserve(visible_len - buffer.len())
                    .map_err(|_| Error::OutOfMemory)?;
            }
            buffer.resize(visible_len, 0u16);
            decode(
                range(data, tile.offset, tile.size)?,
                plan.tile_width,
                plan.tile_height,
                tile.visible_width as u32,
                tile.visible_height as u32,
                &mut buffer,
            )?;
            for row in 0..tile.visible_height {
                let from = row.checked_mul(tile.visible_width).ok_or(Error::Invalid)?;
                let to = tile
                    .y
                    .checked_add(row)
                    .and_then(|y| y.checked_mul(plan.width as usize))
                    .and_then(|start| start.checked_add(tile.x))
                    .ok_or(Error::Invalid)?;
                let source = buffer
                    .get(from..from.checked_add(tile.visible_width).ok_or(Error::Invalid)?)
                    .ok_or(Error::Invalid)?;
                let dest = pixels
                    .get_mut(to..to.checked_add(tile.visible_width).ok_or(Error::Invalid)?)
                    .ok_or(Error::Invalid)?;
                dest.copy_from_slice(source);
            }
        }
        if plan.stage3_supported {
            plan.stage3_constant = constant_pixels(&pixels, plan.width, plan.height)?.map(|rgb| {
                rgb.map(|sample| linearized_sample(plan.linearization.as_ref(), sample))
            });
        }
        plan.tiles = Vec::new();
        Ok(Some(Self {
            bytes: Vec::new(),
            plan,
            pixels: Some(pixels),
        }))
    }

    pub fn inflate(
        bytes: Vec<u8>,
        mut plan: Plan,
        mut validate: impl FnMut(&[u8], usize) -> Result<(), Error>,
        mut decompress: impl FnMut(&[u8], &mut [u8]) -> Result<(), Error>,
    ) -> Result<Self, Error> {
        if plan.compression != 8 || !plan.tiles.is_empty() {
            return Err(Error::Unsupported);
        }
        let row_bytes = (plan.width as usize).checked_mul(2).ok_or(Error::Invalid)?;
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
            let dest = decoded
                .get_mut(start..start.checked_add(size).ok_or(Error::Invalid)?)
                .ok_or(Error::Invalid)?;
            decompress(range(&bytes, strip.offset, strip.size)?, dest)?;
            if plan.predictor == 2 {
                reverse_horizontal_prediction(dest, row_bytes, plan.order, 1)?;
            }
            strip.offset = start;
            strip.size = size;
        }
        if plan.stage3_supported {
            plan.stage3_constant = constant_field(
                &decoded,
                plan.order,
                plan.width,
                plan.height,
                plan.rows_per_strip,
                &plan.strips,
            )?
            .map(|rgb| rgb.map(|sample| linearized_sample(plan.linearization.as_ref(), sample)));
        }
        Ok(Self {
            bytes: decoded,
            plan,
            pixels: None,
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

    pub fn stage3_rgb_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage3_status()?;
        let width = self.plan.width as usize;
        if row >= self.plan.height || output.len() != width.checked_mul(3).ok_or(Error::Invalid)? {
            return Err(Error::Invalid);
        }
        if let Some(rgb) = self.plan.stage3_constant {
            return demosaic::constant_row(rgb, width, output);
        }
        let count = width.checked_mul(3).ok_or(Error::Invalid)?;
        let mut samples = Vec::new();
        samples
            .try_reserve_exact(count)
            .map_err(|_| Error::OutOfMemory)?;
        samples.resize(count, 0);
        let (previous, remaining) = samples.split_at_mut(width);
        let (current, next) = remaining.split_at_mut(width);
        if row > 0 {
            self.stage2_row(row - 1, previous)?;
        }
        self.stage2_row(row, current)?;
        if row + 1 < self.plan.height {
            self.stage2_row(row + 1, next)?;
        }
        demosaic::bilinear_row_from_neighbors(
            (row > 0).then_some(&*previous),
            current,
            (row + 1 < self.plan.height).then_some(&*next),
            width,
            self.plan.height as usize,
            row as usize,
            output,
        )
    }

    fn bytes_row(&self, row: u32) -> Result<&[u8], Error> {
        if row >= self.plan.height {
            return Err(Error::Invalid);
        }
        let stride = (self.plan.width as usize)
            .checked_mul(2)
            .ok_or(Error::Invalid)?;
        let strip = &self.plan.strips[(row / self.plan.rows_per_strip) as usize];
        let offset = ((row % self.plan.rows_per_strip) as usize)
            .checked_mul(stride)
            .and_then(|n| strip.offset.checked_add(n))
            .ok_or(Error::Invalid)?;
        range(&self.bytes, offset, stride)
    }
    pub fn raw_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        if output.len() != self.plan.width as usize {
            return Err(Error::Invalid);
        }
        if let Some(pixels) = &self.pixels {
            let start = (row as usize)
                .checked_mul(self.plan.width as usize)
                .ok_or(Error::Invalid)?;
            let source = pixels
                .get(start..start.checked_add(output.len()).ok_or(Error::Invalid)?)
                .ok_or(Error::Invalid)?;
            output.copy_from_slice(source);
            return Ok(());
        }
        let bytes = self.bytes_row(row)?;
        for (src, dest) in bytes.chunks_exact(2).zip(output.iter_mut()) {
            *dest = self.plan.order.u16(src);
        }
        Ok(())
    }
    pub fn stage2_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage2_status()?;
        if output.len() != self.plan.width as usize {
            return Err(Error::Invalid);
        }
        self.raw_row(row, output)?;
        let (max_num, max_den) = self.plan.max_black;
        for (column, dest) in output.iter_mut().enumerate() {
            let sample = u128::from(linearized_sample(self.plan.linearization.as_ref(), *dest));
            let index = ((row & 1) * 2 + (column as u32 & 1)) as usize;
            let (local_num, local_den) = self.plan.black[index];
            let value = sample * u128::from(local_den);
            let low = u128::from(local_num);
            if value <= low {
                *dest = 0;
                continue;
            }
            // The DNG plane's maximum repeating black sets the scale
            // denominator; each pixel still subtracts its local black.
            let span = (u128::from(self.plan.white) * u128::from(max_den) - u128::from(max_num))
                * u128::from(local_den);
            let scaled = (value - low) * u128::from(max_den) * u128::from(u16::MAX);
            let top = span * u128::from(u16::MAX);
            *dest = if scaled >= top {
                u16::MAX
            } else {
                ((scaled + span / 2) / span) as u16
            };
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{Error, Image, Plan};
    use std::cell::Cell;

    const RAW: [[u16; 4]; 4] = [
        [256, 512, 0, 4095],
        [1692, 1729, 1766, 1803],
        [2284, 2321, 2358, 2395],
        [2876, 2913, 2950, 2987],
    ];
    const NORMALIZED: [[u16; 4]; 4] = [
        [0, 0, 0, 65535],
        [25181, 15045, 26760, 16624],
        [43277, 38604, 44857, 40183],
        [50448, 40311, 52027, 41890],
    ];

    fn fixture(big_endian: bool, multi: bool) -> Vec<u8> {
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
        for i in 0..9 {
            matrix.extend_from_slice(&rational(if i % 4 == 0 { 1 } else { 0 }));
        }
        let mut area = Vec::new();
        for n in [0, 0, 4, 4] {
            area.extend_from_slice(&long(n));
        }
        let mut fields = vec![
            (254, 4, 1, long(0).to_vec()),
            (256, 4, 1, long(4).to_vec()),
            (257, 4, 1, long(4).to_vec()),
            (258, 3, 1, word(16).to_vec()),
            (259, 3, 1, word(1).to_vec()),
            (262, 3, 1, word(32803).to_vec()),
            (
                273,
                4,
                if multi { 2 } else { 1 },
                if multi { vec![0; 8] } else { long(0).to_vec() },
            ),
            (274, 3, 1, word(1).to_vec()),
            (277, 3, 1, word(1).to_vec()),
            (278, 4, 1, long(if multi { 2 } else { 4 }).to_vec()),
            (
                279,
                4,
                if multi { 2 } else { 1 },
                if multi {
                    [long(16), long(16)].concat()
                } else {
                    long(32).to_vec()
                },
            ),
            (284, 3, 1, word(1).to_vec()),
            (339, 3, 1, word(1).to_vec()),
            (33421, 3, 2, [word(2); 2].concat()),
            (33422, 1, 4, vec![0, 1, 1, 2]),
            (50706, 1, 4, vec![1, 4, 0, 0]),
            (50707, 1, 4, vec![1, 1, 0, 0]),
            (50708, 2, 6, b"Bayer\0".to_vec()),
            (50710, 1, 3, vec![0, 1, 2]),
            (50711, 3, 1, word(1).to_vec()),
            (50713, 3, 2, [word(2); 2].concat()),
            (
                50714,
                5,
                4,
                [rational(256), rational(512), rational(512), rational(1024)].concat(),
            ),
            (50717, 4, 1, long(4095).to_vec()),
            (50718, 5, 2, pair(1, 1)),
            (50719, 5, 2, pair(0, 0)),
            (50720, 5, 2, pair(4, 4)),
            (50721, 10, 9, matrix),
            (
                50727,
                5,
                3,
                [rational(1), rational(1), rational(1)].concat(),
            ),
            (
                50728,
                5,
                3,
                [rational(1), rational(1), rational(1)].concat(),
            ),
            (50778, 3, 1, word(21).to_vec()),
            (50829, 4, 4, area),
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
                    offsets = offset;
                }
            }
        }
        for strip in 0..if multi { 2 } else { 1 } {
            if bytes.len() % 2 != 0 {
                bytes.push(0);
            }
            let at = if multi { offsets + strip * 4 } else { inline };
            let offset = bytes.len() as u32;
            bytes[at..at + 4].copy_from_slice(&long(offset));
            let start = if strip == 0 { 0 } else { 2 };
            let end = if multi && strip == 0 { 2 } else { 4 };
            for row in &RAW[start..end] {
                for &value in row {
                    bytes.extend_from_slice(&word(value));
                }
            }
        }
        bytes
    }

    fn deflate_fixture(big_endian: bool, multi: bool, predictor: u16) -> Vec<u8> {
        let mut bytes = fixture(big_endian, multi);
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
                .map(|i| 10 + i * 12)
                .find(|&at| data[at..at + 2] == word(id))
                .expect("known tag")
        };
        let compression = find(259, &bytes);
        bytes[compression + 8..compression + 10].copy_from_slice(&word(8));
        let predictor_tag = find(50728, &bytes);
        bytes[predictor_tag..predictor_tag + 12].copy_from_slice(
            &[
                word(317).as_slice(),
                word(3).as_slice(),
                long(1).as_slice(),
                word(predictor).as_slice(),
                &[0, 0],
            ]
            .concat(),
        );
        let black = find(50714, &bytes);
        let levels = if big_endian {
            u32::from_be_bytes(
                bytes[black + 8..black + 12]
                    .try_into()
                    .expect("black levels"),
            )
        } else {
            u32::from_le_bytes(
                bytes[black + 8..black + 12]
                    .try_into()
                    .expect("black levels"),
            )
        } as usize;
        for i in 0..4 {
            bytes[levels + i * 8..levels + i * 8 + 4].copy_from_slice(&long(0));
        }
        let white = find(50717, &bytes);
        bytes[white + 8..white + 12].copy_from_slice(&long(65535));
        if predictor == 2 {
            let offsets_tag = find(273, &bytes);
            let offsets = if multi {
                let offset = if big_endian {
                    u32::from_be_bytes(
                        bytes[offsets_tag + 8..offsets_tag + 12]
                            .try_into()
                            .expect("offset"),
                    )
                } else {
                    u32::from_le_bytes(
                        bytes[offsets_tag + 8..offsets_tag + 12]
                            .try_into()
                            .expect("offset"),
                    )
                } as usize;
                (0..2)
                    .map(|i| {
                        let at = offset + 4 * i;
                        (if big_endian {
                            u32::from_be_bytes(bytes[at..at + 4].try_into().expect("strip"))
                        } else {
                            u32::from_le_bytes(bytes[at..at + 4].try_into().expect("strip"))
                        }) as usize
                    })
                    .collect::<Vec<_>>()
            } else {
                vec![
                    (if big_endian {
                        u32::from_be_bytes(
                            bytes[offsets_tag + 8..offsets_tag + 12]
                                .try_into()
                                .expect("strip"),
                        )
                    } else {
                        u32::from_le_bytes(
                            bytes[offsets_tag + 8..offsets_tag + 12]
                                .try_into()
                                .expect("strip"),
                        )
                    }) as usize,
                ]
            };
            for (y, row) in RAW.iter().enumerate() {
                let start =
                    offsets[if multi { y / 2 } else { 0 }] + (y % if multi { 2 } else { 4 }) * 8;
                for x in 1..4 {
                    bytes[start + 2 * x..start + 2 * x + 2]
                        .copy_from_slice(&word(row[x].wrapping_sub(row[x - 1])));
                }
            }
        }
        bytes
    }

    fn linearized_fixture(big_endian: bool, multi: bool, compressed: bool, short: bool) -> Vec<u8> {
        let mut bytes = deflate_fixture(big_endian, multi, 1);
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
        if !compressed {
            let compression = find(259, &bytes);
            bytes[compression + 8..compression + 10].copy_from_slice(&word(1));
        }
        let analog_balance = find(50727, &bytes);
        bytes[analog_balance..analog_balance + 2].copy_from_slice(&word(50712));
        bytes[analog_balance + 2..analog_balance + 4].copy_from_slice(&word(3));
        if short {
            bytes[analog_balance + 4..analog_balance + 8].copy_from_slice(&long(2));
            bytes[analog_balance + 8..analog_balance + 12]
                .copy_from_slice(&[word(0), word(u16::MAX)].concat());
        } else {
            if bytes.len() & 1 != 0 {
                bytes.push(0);
            }
            let start = bytes.len();
            for sample in 0..=u16::MAX {
                bytes.extend_from_slice(&word(sample.saturating_mul(2)));
            }
            bytes[analog_balance + 4..analog_balance + 8].copy_from_slice(&long(65536));
            bytes[analog_balance + 8..analog_balance + 12].copy_from_slice(&long(start as u32));
        }
        bytes
    }

    #[test]
    fn linearization_table_maps_checked_stage2_and_stage3_rows() {
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
                        .expect("validated table")
                        .expect("RGGB profile");
                        let image = if compressed {
                            Image::inflate(
                                bytes,
                                plan,
                                |encoded, expected| {
                                    assert_eq!(encoded.len(), expected);
                                    Ok(())
                                },
                                |encoded, dest| {
                                    dest.copy_from_slice(encoded);
                                    Ok(())
                                },
                            )
                            .expect("checked compressed rows")
                        } else {
                            Image::new(bytes, plan)
                        };
                        assert_eq!(image.stage2_status(), Ok(()));
                        assert_eq!(image.stage3_status(), Ok(()));
                        for y in 0..4 {
                            let mut raw = [0xa5a5; 5];
                            let mut linear = [0xa5a5; 5];
                            image.raw_row(y, &mut raw[..4]).expect("unmapped raw row");
                            image
                                .stage2_row(y, &mut linear[..4])
                                .expect("linearized row");
                            assert_eq!(raw[..4], RAW[y as usize]);
                            for x in 0..4 {
                                let expected = if short {
                                    if raw[x] == 0 {
                                        0
                                    } else {
                                        u16::MAX
                                    }
                                } else {
                                    raw[x].saturating_mul(2)
                                };
                                assert_eq!(linear[x], expected);
                            }
                            assert_eq!((raw[4], linear[4]), (0xa5a5, 0xa5a5));
                        }
                        let mut rgb = [0xa5a5; 13];
                        image
                            .stage3_rgb_row(0, &mut rgb[..12])
                            .expect("linearized demosaic");
                        assert_eq!(rgb[0], if short { u16::MAX } else { RAW[0][0] * 2 });
                        assert_eq!(rgb[12], 0xa5a5);
                    }
                }
            }
        }

        let mut bytes = linearized_fixture(false, false, false, false);
        let pixels = field(&bytes, 273);
        let offset =
            u32::from_le_bytes(bytes[pixels + 8..pixels + 12].try_into().expect("strip")) as usize;
        for y in 0..4 {
            for x in 0..4 {
                let sample = match (y & 1, x & 1) {
                    (0, 0) => 1000u16,
                    (1, 1) => 3000,
                    _ => 2000,
                };
                bytes[offset + (y * 4 + x) * 2..offset + (y * 4 + x) * 2 + 2]
                    .copy_from_slice(&sample.to_le_bytes());
            }
        }
        let plan = Plan::parse(&bytes).expect("constant input").expect("RGGB");
        let image = Image::new(bytes, plan);
        let mut rgb = [0xa5a5; 13];
        image
            .stage3_rgb_row(0, &mut rgb[..12])
            .expect("constant Stage3");
        for pixel in rgb[..12].chunks_exact(3) {
            assert_eq!(pixel, &[2000, 4000, 6000]);
        }
        assert_eq!(rgb[12], 0xa5a5);
    }

    #[test]
    fn malformed_linearization_and_unverified_black_fail_closed() {
        let good = linearized_fixture(false, false, false, false);
        let table = field(&good, 50712);
        let mut bytes = good.clone();
        bytes[table + 2..table + 4].copy_from_slice(&4u16.to_le_bytes());
        bytes[table + 4..table + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[table + 4..table + 8].copy_from_slice(&0u32.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Invalid)));
        bytes = good.clone();
        bytes[table + 8..table + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Plan::parse(&bytes), Err(Error::Incomplete)));

        bytes = good;
        let white = field(&bytes, 50717);
        bytes[white + 8..white + 12].copy_from_slice(&4095u32.to_le_bytes());
        let plan = Plan::parse(&bytes).expect("Stage1 table").expect("RGGB");
        let image = Image::new(bytes, plan);
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut untouched = [0xa5a5; 4];
        assert_eq!(image.stage2_row(0, &mut untouched), Err(Error::Unsupported));
        assert_eq!(untouched, [0xa5a5; 4]);
    }

    fn field(bytes: &[u8], id: u16) -> usize {
        let count = u16::from_le_bytes(bytes[8..10].try_into().expect("IFD count"));
        (0..count as usize)
            .map(|i| 10 + 12 * i)
            .find(|&at| u16::from_le_bytes(bytes[at..at + 2].try_into().expect("tag")) == id)
            .expect("test field")
    }

    #[test]
    fn deflate_strips_preflight_and_predictor_rows() {
        for big_endian in [false, true] {
            for multi in [false, true] {
                for predictor in [1, 2] {
                    let bytes = deflate_fixture(big_endian, multi, predictor);
                    assert!(Plan::parse(&bytes).expect("uncompressed parser").is_none());
                    let plan = Plan::parse_deflate(&bytes)
                        .expect("checked metadata")
                        .expect("Bayer Deflate");
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
                            output.copy_from_slice(encoded);
                            decodes.set(decodes.get() + 1);
                            Ok(())
                        },
                    )
                    .expect("decoded Bayer strips");
                    assert_eq!(
                        (checks.get(), decodes.get()),
                        (if multi { 2 } else { 1 }, if multi { 2 } else { 1 })
                    );
                    assert_eq!(image.stage2_status(), Ok(()));
                    assert_eq!(image.stage3_status(), Ok(()));
                    for y in 0..4 {
                        let mut first = [0xa5a5; 5];
                        let mut second = [0xa5a5; 5];
                        image.raw_row(y, &mut first[..4]).expect("raw row");
                        image
                            .stage2_row(y, &mut second[..4])
                            .expect("normalized row");
                        assert_eq!(first[..4], RAW[y as usize]);
                        assert_eq!(second[..4], RAW[y as usize]);
                        assert_eq!((first[4], second[4]), (0xa5a5, 0xa5a5));
                    }
                    let mut rgb = [0xa5a5; 13];
                    image
                        .stage3_rgb_row(0, &mut rgb[..12])
                        .expect("guarded demosaic");
                    assert_eq!(rgb[0], RAW[0][0]);
                    assert_eq!(rgb[12], 0xa5a5);
                }
            }
        }
    }

    #[test]
    fn deflate_rejects_bad_predictor_and_preflight_before_decode() {
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
            .expect("checked metadata")
            .expect("Bayer Deflate");
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

        let mut bytes = deflate_fixture(false, false, 1);
        let black = field(&bytes, 50714);
        let levels =
            u32::from_le_bytes(bytes[black + 8..black + 12].try_into().expect("offset")) as usize;
        bytes[levels..levels + 4].copy_from_slice(&256u32.to_le_bytes());
        let plan = Plan::parse_deflate(&bytes)
            .expect("valid nonzero black")
            .expect("Bayer Deflate");
        let image = Image::inflate(
            bytes,
            plan,
            |_, _| Ok(()),
            |encoded, dest| {
                dest.copy_from_slice(encoded);
                Ok(())
            },
        )
        .expect("raw samples still available");
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        assert_eq!(image.stage3_status(), Err(Error::Unsupported));
        let mut row = [0xa5a5; 4];
        image.raw_row(0, &mut row).expect("Stage 1");
        assert_eq!(row, RAW[0]);
        assert_eq!(image.stage2_row(0, &mut row), Err(Error::Unsupported));
        assert_eq!(row, RAW[0]);
    }

    fn tiled_fixture(big_endian: bool) -> Vec<u8> {
        let mut bytes = fixture(big_endian, true);
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
        let tag = |data: &[u8], id: u16| {
            let count = if big_endian {
                u16::from_be_bytes(data[8..10].try_into().expect("count"))
            } else {
                u16::from_le_bytes(data[8..10].try_into().expect("count"))
            };
            (0..count as usize)
                .map(|i| 10 + 12 * i)
                .find(|&at| data[at..at + 2] == word(id))
                .expect("tag")
        };
        let compression = tag(&bytes, 259);
        bytes[compression + 8..compression + 10].copy_from_slice(&word(7));
        for (old, new) in [(273, 324), (274, 323), (278, 322), (279, 325)] {
            let at = tag(&bytes, old);
            bytes[at..at + 2].copy_from_slice(&word(new));
            if new == 322 || new == 323 {
                bytes[at + 2..at + 4].copy_from_slice(&word(4));
                bytes[at + 8..at + 12].copy_from_slice(&long(if new == 322 { 3 } else { 4 }));
            }
        }
        bytes
    }

    #[test]
    fn checked_sof3_tile_placement_and_stage2() {
        for big_endian in [false, true] {
            let bytes = tiled_fixture(big_endian);
            let checks = Cell::new(0);
            let decodes = Cell::new(0);
            let image = Image::parse_tiled(
                &bytes,
                |jpeg, width, height| {
                    assert_eq!((jpeg.len(), width, height), (16, 3, 4));
                    checks.set(checks.get() + 1);
                    Ok(())
                },
                |_jpeg, width, height, visible_width, visible_height, tile| {
                    assert_eq!((width, height, visible_height), (3, 4, 4));
                    let column = decodes.get() * 3;
                    assert_eq!(visible_width, if column == 0 { 3 } else { 1 });
                    for y in 0..4 {
                        for x in 0..visible_width as usize {
                            tile[y * visible_width as usize + x] = RAW[y][column + x];
                        }
                    }
                    decodes.set(decodes.get() + 1);
                    Ok(())
                },
            )
            .expect("valid tile metadata")
            .expect("Bayer tiled path");
            assert_eq!((checks.get(), decodes.get()), (2, 2));
            assert_eq!(image.stage2_status(), Ok(()));
            assert_eq!(image.stage3_status(), Err(Error::Unsupported));
            for y in 0..4 {
                let mut raw = [0xa5a5u16; 5];
                let mut normalized = [0xa5a5u16; 5];
                image.raw_row(y, &mut raw[..4]).expect("raw row");
                image
                    .stage2_row(y, &mut normalized[..4])
                    .expect("stage2 row");
                assert_eq!(raw[..4], RAW[y as usize]);
                assert_eq!(normalized[..4], NORMALIZED[y as usize]);
                assert_eq!((raw[4], normalized[4]), (0xa5a5, 0xa5a5));
            }
        }
    }

    #[test]
    fn tiled_uniform_field_has_guarded_rgb_stage3() {
        for big_endian in [false, true] {
            for varied in [false, true] {
                let mut bytes = tiled_fixture(big_endian);
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
                let tag = |data: &[u8], id: u16| {
                    let count = if big_endian {
                        u16::from_be_bytes(data[8..10].try_into().expect("count"))
                    } else {
                        u16::from_le_bytes(data[8..10].try_into().expect("count"))
                    };
                    (0..count as usize)
                        .map(|i| 10 + 12 * i)
                        .find(|&at| data[at..at + 2] == word(id))
                        .expect("tag")
                };
                let white = tag(&bytes, 50717);
                bytes[white + 8..white + 12].copy_from_slice(&long(65535));
                let black = tag(&bytes, 50714);
                let black_at = if big_endian {
                    u32::from_be_bytes(bytes[black + 8..black + 12].try_into().expect("offset"))
                } else {
                    u32::from_le_bytes(bytes[black + 8..black + 12].try_into().expect("offset"))
                } as usize;
                for index in 0..4 {
                    bytes[black_at + index * 8..black_at + index * 8 + 4].copy_from_slice(&long(0));
                }
                let tile_index = Cell::new(0);
                let image = Image::parse_tiled(
                    &bytes,
                    |_, _, _| Ok(()),
                    |_, _, _, visible_width, visible_height, output| {
                        let x_start = tile_index.get() * 3;
                        for y in 0..visible_height as usize {
                            for x in 0..visible_width as usize {
                                let global_x = x_start + x;
                                output[y * visible_width as usize + x] = match (y & 1, global_x & 1)
                                {
                                    (0, 0) => 10000,
                                    (1, 1) => 30000,
                                    _ => 20000 + u16::from(varied && y == 0 && global_x == 1),
                                };
                            }
                        }
                        tile_index.set(tile_index.get() + 1);
                        Ok(())
                    },
                )
                .expect("valid tiled field")
                .expect("SOF3 tiled Bayer path");
                assert_eq!(tile_index.get(), 2);
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Ok(()));
                for y in 0..4 {
                    let mut rgb = [0xa5a5; 13];
                    image.stage3_rgb_row(y, &mut rgb[..12]).expect("Stage 3");
                    if !varied {
                        for pixel in rgb[..12].chunks_exact(3) {
                            assert_eq!(pixel, [10000, 20000, 30000]);
                        }
                    } else if y == 0 {
                        assert_eq!(rgb[1], 20001);
                    }
                    assert_eq!(rgb[12], 0xa5a5);
                }
            }
        }
    }

    #[test]
    fn tiled_preflight_and_decoder_errors_fail_before_publishing() {
        let bytes = tiled_fixture(false);
        let tile_tag = field(&bytes, 324);
        let offsets = u32::from_le_bytes(
            bytes[tile_tag + 8..tile_tag + 12]
                .try_into()
                .expect("tile offsets pointer"),
        ) as usize;
        let mut missing = bytes.clone();
        missing[offsets + 4..offsets + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        let calls = Cell::new(0);
        assert!(matches!(
            Image::parse_tiled(
                &missing,
                |_, _, _| {
                    calls.set(calls.get() + 1);
                    Ok(())
                },
                |_, _, _, _, _, _| Ok(()),
            ),
            Err(Error::Incomplete)
        ));
        assert_eq!(calls.get(), 0);

        let mut count = bytes.clone();
        count[tile_tag + 4..tile_tag + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(
            Image::parse_tiled(&count, |_, _, _| Ok(()), |_, _, _, _, _, _| Ok(())),
            Err(Error::Invalid)
        ));
        let mut corrupted = bytes.clone();
        let lengths_tag = field(&corrupted, 325);
        corrupted[lengths_tag + 2..lengths_tag + 4].copy_from_slice(&2u16.to_le_bytes());
        assert!(matches!(
            Image::parse_tiled(&corrupted, |_, _, _| Ok(()), |_, _, _, _, _, _| Ok(())),
            Err(Error::Invalid)
        ));
        assert!(matches!(
            Image::parse_tiled(
                &bytes,
                |_, _, _| Err(Error::Unsupported),
                |_, _, _, _, _, _| panic!("decode must not follow failed validation")
            ),
            Err(Error::Unsupported)
        ));
    }

    #[test]
    fn rg_gb_stage1_2_endian_multistrip_and_no_stage3() {
        for big_endian in [false, true] {
            for multi in [false, true] {
                let bytes = fixture(big_endian, multi);
                let plan = Plan::parse(&bytes)
                    .expect("valid CFA")
                    .expect("Bayer route");
                let image = Image::new(bytes, plan);
                assert_eq!((image.width(), image.height()), (4, 4));
                assert_eq!(image.stage2_status(), Ok(()));
                assert_eq!(image.stage3_status(), Err(Error::Unsupported));
                for repeat in 0..2 {
                    for row in 0..4 {
                        let mut raw = [0u16; 4];
                        let mut stage2 = [0u16; 4];
                        image.raw_row(row, &mut raw).expect("stage 1");
                        image.stage2_row(row, &mut stage2).expect("stage 2");
                        assert_eq!(raw, RAW[row as usize], "repeat {repeat}");
                        assert_eq!(stage2, NORMALIZED[row as usize]);
                    }
                }
                let mut wrong = [0xa5a5; 3];
                assert_eq!(image.raw_row(0, &mut wrong), Err(Error::Invalid));
                assert_eq!(wrong, [0xa5a5; 3]);
            }
        }
    }

    #[test]
    fn constant_rggb_field_has_guarded_rgb_stage3() {
        let mut bytes = fixture(false, true);
        let black_tag = field(&bytes, 50714);
        let levels = u32::from_le_bytes(
            bytes[black_tag + 8..black_tag + 12]
                .try_into()
                .expect("black levels pointer"),
        ) as usize;
        for i in 0..4 {
            bytes[levels + i * 8..levels + i * 8 + 4].copy_from_slice(&0u32.to_le_bytes());
        }
        let white_tag = field(&bytes, 50717);
        bytes[white_tag + 8..white_tag + 12].copy_from_slice(&65535u32.to_le_bytes());
        let offset_tag = field(&bytes, 273);
        let offsets = u32::from_le_bytes(
            bytes[offset_tag + 8..offset_tag + 12]
                .try_into()
                .expect("strip offsets pointer"),
        ) as usize;
        for y in 0..4 {
            let strip = y / 2;
            let base = u32::from_le_bytes(
                bytes[offsets + strip * 4..offsets + strip * 4 + 4]
                    .try_into()
                    .expect("strip offset"),
            ) as usize;
            for x in 0..4 {
                let value = match (y & 1, x & 1) {
                    (0, 0) => 10000u16,
                    (1, 1) => 30000,
                    _ => 20000,
                };
                let at = base + (y % 2 * 4 + x) * 2;
                bytes[at..at + 2].copy_from_slice(&value.to_le_bytes());
            }
        }
        let plan = Plan::parse(&bytes)
            .expect("valid uniform sensor")
            .expect("Bayer");
        let image = Image::new(bytes, plan);
        assert_eq!(image.stage2_status(), Ok(()));
        assert_eq!(image.stage3_status(), Ok(()));
        for y in 0..4 {
            let mut row = [0xa5a5u16; 14];
            image
                .stage3_rgb_row(y, &mut row[..12])
                .expect("Stage 3 RGB");
            for rgb in row[..12].chunks_exact(3) {
                assert_eq!(rgb, [10000, 20000, 30000]);
            }
            assert_eq!(row[12..], [0xa5a5; 2]);
        }
        let mut wrong = [0xa5a5u16; 11];
        assert_eq!(image.stage3_rgb_row(0, &mut wrong), Err(Error::Invalid));
        assert_eq!(wrong, [0xa5a5; 11]);
        assert_eq!(
            image.stage3_rgb_row(4, &mut [0u16; 12]),
            Err(Error::Invalid)
        );

        let mut opcode3 = image.bytes.clone();
        let illuminant = field(&opcode3, 50778);
        opcode3[illuminant..illuminant + 2].copy_from_slice(&51022u16.to_le_bytes());
        opcode3[illuminant + 2..illuminant + 4].copy_from_slice(&7u16.to_le_bytes());
        let plan = Plan::parse(&opcode3)
            .expect("Stage 1 accepts a later-stage opcode")
            .expect("Bayer");
        let guarded = Image::new(opcode3, plan);
        assert_eq!(guarded.stage2_status(), Ok(()));
        assert_eq!(guarded.stage3_status(), Err(Error::Unsupported));
        let mut untouched = [0xa5a5u16; 12];
        assert_eq!(
            guarded.stage3_rgb_row(0, &mut untouched),
            Err(Error::Unsupported)
        );
        assert_eq!(untouched, [0xa5a5; 12]);

        let mut varied = image.bytes.clone();
        let offsets_tag = field(&varied, 273);
        let offsets = u32::from_le_bytes(
            varied[offsets_tag + 8..offsets_tag + 12]
                .try_into()
                .expect("strip offsets pointer"),
        ) as usize;
        let first_strip = u32::from_le_bytes(
            varied[offsets..offsets + 4]
                .try_into()
                .expect("first strip offset"),
        ) as usize;
        varied[first_strip + 4..first_strip + 6].copy_from_slice(&10001u16.to_le_bytes());
        let plan = Plan::parse(&varied)
            .expect("valid varied sensor")
            .expect("Bayer");
        let varied_image = Image::new(varied, plan);
        assert_eq!(varied_image.stage2_status(), Ok(()));
        assert_eq!(varied_image.stage3_status(), Ok(()));
        varied_image
            .stage3_rgb_row(0, &mut untouched)
            .expect("varied Stage 3");
        assert_eq!(untouched[2 * 3], 10001);
    }

    #[test]
    fn malformed_or_unimplemented_sensor_metadata_fails_closed() {
        let bytes = fixture(false, true);
        assert!(matches!(
            Plan::parse(&bytes[..bytes.len() - 1]),
            Err(Error::Incomplete)
        ));

        let offsets_field = field(&bytes, 273);
        let offsets = u32::from_le_bytes(
            bytes[offsets_field + 8..offsets_field + 12]
                .try_into()
                .expect("offset pointer"),
        ) as usize;
        let mut missing = bytes.clone();
        missing[offsets + 4..offsets + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Plan::parse(&missing), Err(Error::Incomplete)));
        let mut count = bytes.clone();
        count[offsets_field + 4..offsets_field + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(Plan::parse(&count), Err(Error::Invalid)));

        let pattern_field = field(&bytes, 33422);
        let mut pattern = bytes.clone();
        pattern[pattern_field + 11] = 0; // Not RGGB.
        assert!(matches!(Plan::parse(&pattern), Err(Error::Unsupported)));

        let mut wrong_type = bytes.clone();
        wrong_type[pattern_field + 2..pattern_field + 4].copy_from_slice(&3u16.to_le_bytes());
        assert!(matches!(
            Plan::parse(&wrong_type),
            Err(Error::Invalid | Error::Incomplete)
        ));

        let mut crop = bytes.clone();
        let crop_field = field(&crop, 50719);
        let crop_data = u32::from_le_bytes(
            crop[crop_field + 8..crop_field + 12]
                .try_into()
                .expect("crop pointer"),
        ) as usize;
        crop[crop_data..crop_data + 4].copy_from_slice(&1u32.to_le_bytes());
        let plan = Plan::parse(&crop).expect("stage 1").expect("Bayer route");
        let image = Image::new(crop, plan);
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        let mut sentinel = [0xa5a5; 4];
        assert_eq!(image.stage2_row(0, &mut sentinel), Err(Error::Unsupported));
        assert_eq!(sentinel, [0xa5a5; 4]);

        let mut opcode = bytes;
        let matrix = field(&opcode, 50721);
        opcode[matrix..matrix + 2].copy_from_slice(&51009u16.to_le_bytes());
        opcode[matrix + 2..matrix + 4].copy_from_slice(&7u16.to_le_bytes());
        opcode[matrix + 4..matrix + 8].copy_from_slice(&4u32.to_le_bytes());
        let plan = Plan::parse(&opcode).expect("stage 1").expect("Bayer route");
        assert_eq!(
            Image::new(opcode, plan).stage2_status(),
            Err(Error::Unsupported)
        );
    }
}
