// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Classic TIFF IFD selection and the narrow lossy-JPEG tiled RGB stage-1 path.
//! The JPEG callback is supplied by the caller; container parsing and placement
//! remain safe Rust and do not depend on a specific JPEG implementation.

use super::dng::{
    identity_array, integral, number, optional, range, required, scalar, tiff_tag,
    valid_sdr_tone_curve, ByteOrder, Error, Tag,
};
use super::opcode::{self, OpcodeList};

pub(crate) struct Ifd<'a> {
    pub(crate) tags: Vec<Tag<'a>>,
    pub(crate) next: u32,
}

pub(crate) fn read_ifd(data: &[u8], offset: usize, order: ByteOrder) -> Result<Ifd<'_>, Error> {
    let count = order.u16(range(data, offset, 2)?) as usize;
    let start = offset.checked_add(2).ok_or(Error::Invalid)?;
    let size = count.checked_mul(12).ok_or(Error::Invalid)?;
    range(data, start, size)?;
    let end = start.checked_add(size).ok_or(Error::Invalid)?;
    let next = order.u32(range(data, end, 4)?);
    let mut tags = Vec::new();
    tags.try_reserve_exact(count)
        .map_err(|_| Error::OutOfMemory)?;
    for i in 0..count {
        let position = start
            .checked_add(i.checked_mul(12).ok_or(Error::Invalid)?)
            .ok_or(Error::Invalid)?;
        tags.push(tiff_tag(data, range(data, position, 12)?, order)?);
    }
    tags.sort_unstable_by_key(|tag| tag.id);
    if tags.windows(2).any(|pair| pair[0].id == pair[1].id) {
        return Err(Error::Invalid);
    }
    Ok(Ifd { tags, next })
}

fn checked_offset(seen: &mut Vec<usize>, offset: u32) -> Result<usize, Error> {
    if offset == 0 {
        return Err(Error::Invalid);
    }
    let offset = offset as usize;
    if seen.contains(&offset) {
        return Err(Error::Invalid);
    }
    seen.try_reserve(1).map_err(|_| Error::OutOfMemory)?;
    seen.push(offset);
    Ok(offset)
}

fn raw_candidate(ifd: &Ifd<'_>, order: ByteOrder) -> Result<bool, Error> {
    let subfile = if let Some(tag) = optional(&ifd.tags, 254) {
        if tag.kind != 4 {
            return Err(Error::Invalid);
        }
        scalar(tag, order)?
    } else {
        0
    };
    if subfile != 0 {
        return Ok(false);
    }
    let Some(photo) = optional(&ifd.tags, 262) else {
        return Err(Error::Invalid);
    };
    if photo.kind != 3 {
        return Err(Error::Invalid);
    }
    match scalar(photo, order)? {
        34892 => Ok(true),
        _ => Err(Error::Unsupported),
    }
}

fn root_stage3_metadata_supported(tags: &[Tag<'_>], order: ByteOrder) -> bool {
    tags.iter().all(|tag| match tag.id {
        50940 => valid_sdr_tone_curve(tag, order),
        51110 => tag.kind == 4 && tag.count == 1 && matches!(scalar(tag, order), Ok(0 | 1)),
        _ => matches!(
            tag.id,
            254 | 256
                | 257
                | 258
                | 259
                | 262
                | 270
                | 271
                | 272
                | 273
                | 274
                | 277
                | 278
                | 279
                | 284
                | 305
                | 306
                | 330
                | 529
                | 530
                | 531
                | 532
                | 700
                | 34665
                | 50706
                | 50707
                | 50708
                | 50721
                | 50722
                | 50723
                | 50724
                | 50727
                | 50728
                | 50730
                | 50731
                | 50732
                | 50734
                | 50739
                | 50778
                | 50779
                | 50781
                | 50936
                | 50941
                | 50964
                | 50965
                | 50966
                | 50967
                | 50969
                | 50970
                | 50971
                | 51089
                | 51111
        ),
    })
}

fn has_root_subifds(data: &[u8], first: u32, order: ByteOrder) -> Result<bool, Error> {
    let start = first as usize;
    let count = order.u16(range(data, start, 2)?) as usize;
    let entries_start = start.checked_add(2).ok_or(Error::Invalid)?;
    let entries_size = count.checked_mul(12).ok_or(Error::Invalid)?;
    let entries = range(data, entries_start, entries_size)?;
    for entry in entries.chunks_exact(12) {
        if order.u16(&entry[..2]) == 330 {
            if order.u16(&entry[2..4]) != 4 || order.u32(&entry[4..8]) == 0 {
                return Err(Error::Invalid);
            }
            if order.u32(&entry[4..8]) > 125 {
                return Err(Error::Unsupported);
            }
            return Ok(true);
        }
    }
    Ok(false)
}

fn root_lossless_candidate(data: &[u8], first: u32, order: ByteOrder) -> Result<bool, Error> {
    let start = first as usize;
    let count = order.u16(range(data, start, 2)?) as usize;
    let entries_start = start.checked_add(2).ok_or(Error::Invalid)?;
    let entries = range(
        data,
        entries_start,
        count.checked_mul(12).ok_or(Error::Invalid)?,
    )?;
    let field = |id: u16| {
        entries
            .chunks_exact(12)
            .find(|entry| order.u16(&entry[..2]) == id)
    };
    let scalar_is = |id, expected| {
        field(id).is_some_and(|entry| {
            order.u16(&entry[2..4]) == 3
                && order.u32(&entry[4..8]) == 1
                && order.u16(&entry[8..10]) == expected
        })
    };
    Ok(field(50706).is_some_and(|entry| {
        order.u16(&entry[2..4]) == 1 && order.u32(&entry[4..8]) == 4 && entry[8..12] == [1, 7, 0, 0]
    }) && scalar_is(259, 7)
        && scalar_is(262, 34892)
        && scalar_is(277, 3)
        && field(254).is_some_and(|entry| {
            order.u16(&entry[2..4]) == 4
                && order.u32(&entry[4..8]) == 1
                && order.u32(&entry[8..12]) == 0
        }))
}

fn root_lossless_tag(tag: &Tag<'_>) -> Result<(), Error> {
    let kind = tag.kind;
    let valid = match tag.id {
        254 | 34665 | 50941 | 50981 | 51089 => kind == 4,
        256 | 257 | 322 | 323 | 324 | 325 | 50717 | 50937 => kind == 3 || kind == 4,
        258 | 259 | 262 | 274 | 277 | 284 | 50713 | 50778 | 50779 => kind == 3,
        271 | 272 | 305 | 306 | 50708 | 50735 | 50931 | 50932 | 50936 | 50942 => kind == 2,
        50706 | 50707 | 50781 | 51111 => kind == 1,
        50714 | 50719 | 50720 => matches!(kind, 3 | 4 | 5),
        50718 | 50727 | 50728 | 50731 | 50732 | 50734 | 50736 | 50738 | 50739 | 50780 => kind == 5,
        50721 | 50722 | 50723 | 50724 | 50730 | 50964 | 50965 => kind == 10,
        50938 | 50939 | 50982 => kind == 11,
        51041 => kind == 12,
        700 => kind == 1 || kind == 7,
        51009 | 51022 | 52550 => kind == 7,
        _ => return Err(Error::Unsupported),
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

fn select_raw_ifd<'a>(
    data: &'a [u8],
    order: ByteOrder,
) -> Result<Option<(Ifd<'a>, u32, bool, bool)>, Error> {
    let first = order.u32(range(data, 4, 4)?);
    if !has_root_subifds(data, first, order)? {
        if !root_lossless_candidate(data, first, order)? {
            return Ok(None);
        }
        let root = read_ifd(data, first as usize, order)?;
        if root.next != 0 {
            return Err(if root.next == first {
                Error::Invalid
            } else {
                Error::Unsupported
            });
        }
        if optional(&root.tags, 51008).is_some() {
            return Err(Error::Unsupported); // OpcodeList1 changes Stage 1.
        }
        let version = required(&root.tags, 50706)?;
        let backward = required(&root.tags, 50707)?;
        if version.kind != 1 || version.count != 4 || backward.kind != 1 || backward.count != 4 {
            return Err(Error::Invalid);
        }
        if version.value != &[1, 7, 0, 0] || backward.value != &[1, 3, 0, 0] {
            return Err(Error::Unsupported);
        }
        let model = required(&root.tags, 50708)?;
        if model.kind != 2 || model.count < 2 || model.value.last() != Some(&0) {
            return Err(Error::Invalid);
        }
        for tag in &root.tags {
            root_lossless_tag(tag)?;
        }
        let stage3_supported = optional(&root.tags, 51022).is_none();
        return Ok(Some((root, 0, true, stage3_supported)));
    }
    let first_ifd = read_ifd(data, first as usize, order)?;
    let subifds = required(&first_ifd.tags, 330)?;
    if subifds.kind != 4 || subifds.count == 0 {
        return Err(Error::Invalid);
    }
    if subifds.count > 125 {
        return Err(Error::Unsupported);
    }
    let version = required(&first_ifd.tags, 50706)?;
    let backward = required(&first_ifd.tags, 50707)?;
    if version.kind != 1 || version.count != 4 || backward.kind != 1 || backward.count != 4 {
        return Err(Error::Invalid);
    }
    if version.value != &[1, 4, 0, 0]
        || backward.value[0] != 1
        || backward.value[1] > 4
        || backward.value[2..] != [0, 0]
    {
        return Err(Error::Unsupported);
    }
    let model = required(&first_ifd.tags, 50708)?;
    if model.kind != 2 || model.count < 2 || model.value.last() != Some(&0) {
        return Err(Error::Invalid);
    }
    let root_stage2_supported = first_ifd.next == 0
        && !first_ifd.tags.iter().any(|tag| {
            matches!(
                tag.id,
                50712 | 50713 | 50714 | 50715 | 50717 | 50738 | 50780 | 50829 | 51009
            )
        })
        && optional(&first_ifd.tags, 50734)
            .is_none_or(|tag| tag.kind == 5 && tag.count == 1 && integral(tag, 0, order) == Ok(1));
    let root_stage3_supported =
        root_stage2_supported && root_stage3_metadata_supported(&first_ifd.tags, order);
    let mut seen = Vec::new();
    let mut current = first;
    let mut selected: Option<(Vec<Tag<'a>>, u32)> = None;
    let mut subifd_total = 0usize;
    let mut ifd_index = 0u32;
    for _ in 0..10 {
        let position = checked_offset(&mut seen, current)?;
        let ifd = read_ifd(data, position, order)?;
        if optional(&ifd.tags, 51008).is_some() {
            return Err(Error::Unsupported); // OpcodeList1 changes stage-1 samples.
        }
        if raw_candidate(&ifd, order)? {
            if selected.replace((ifd.tags, ifd_index)).is_some() {
                return Err(Error::Unsupported);
            }
        } else if let Some(subifds) = optional(&ifd.tags, 330) {
            if subifds.kind != 4 || subifds.count == 0 {
                return Err(Error::Invalid);
            }
            subifd_total = subifd_total
                .checked_add(subifds.count as usize)
                .ok_or(Error::Invalid)?;
            if subifd_total > 125 {
                return Err(Error::Unsupported);
            }
            for i in 0..subifds.count as usize {
                let child_offset = number(subifds, i, order)?;
                let child_position = checked_offset(&mut seen, child_offset)?;
                let child = read_ifd(data, child_position, order)?;
                if child.next != 0 || optional(&child.tags, 330).is_some() {
                    return Err(Error::Unsupported);
                }
                if optional(&child.tags, 51008).is_some() {
                    return Err(Error::Unsupported);
                }
                if raw_candidate(&child, order)? {
                    if selected
                        .replace((child.tags, ifd_index + 1 + i as u32))
                        .is_some()
                    {
                        return Err(Error::Unsupported);
                    }
                }
            }
            ifd_index = ifd_index.checked_add(subifds.count).ok_or(Error::Invalid)?;
        }
        if ifd.next == 0 {
            return selected
                .map(|(tags, index)| {
                    Some((
                        Ifd { tags, next: 0 },
                        index,
                        root_stage2_supported,
                        root_stage3_supported,
                    ))
                })
                .ok_or(Error::Unsupported);
        }
        ifd_index = ifd_index.checked_add(1).ok_or(Error::Invalid)?;
        current = ifd.next;
    }
    Err(Error::Unsupported)
}

pub struct RgbImage {
    pixels: Vec<u8>,
    stage2: Result<OpcodeList, Error>,
    stage3_supported: bool,
    pub width: u32,
    pub height: u32,
    pub main_ifd_index: u32,
}

fn parse_stage2(
    tags: &[Tag<'_>],
    order: ByteOrder,
    width: u32,
    height: u32,
    root_supported: bool,
) -> Result<OpcodeList, Error> {
    if !root_supported {
        return Err(Error::Unsupported);
    }
    for id in [50738, 50780] {
        if let Some(tag) = optional(tags, id) {
            if tag.kind != 5 || tag.count != 1 {
                return Err(Error::Invalid);
            }
            if integral(tag, 0, order) != Ok(1) {
                return Err(Error::Unsupported);
            }
        }
    }
    let opcodes = optional(tags, 51009).ok_or(Error::Unsupported)?;
    if opcodes.kind != 7 || opcodes.count < 4 {
        return Err(Error::Invalid);
    }
    let plan = OpcodeList::parse(opcodes.value, width, height, 3).map_err(|error| match error {
        opcode::Error::Invalid => Error::Invalid,
        opcode::Error::Unsupported => Error::Unsupported,
        opcode::Error::OutOfMemory => Error::OutOfMemory,
    })?;
    if plan.is_empty() {
        return Err(Error::Unsupported);
    }
    Ok(plan)
}

fn visible_tile(
    width: u32,
    height: u32,
    tile_width: u32,
    tile_height: u32,
    tx: u32,
    ty: u32,
) -> Result<(usize, usize, usize, usize, usize), Error> {
    let x = tx.checked_mul(tile_width).ok_or(Error::Invalid)?;
    let y = ty.checked_mul(tile_height).ok_or(Error::Invalid)?;
    if x >= width || y >= height {
        return Err(Error::Invalid);
    }
    let visible_width = (width - x).min(tile_width) as usize;
    let visible_height = (height - y).min(tile_height) as usize;
    let len = visible_width
        .checked_mul(visible_height)
        .and_then(|n| n.checked_mul(3))
        .ok_or(Error::Invalid)?;
    Ok((x as usize, y as usize, visible_width, visible_height, len))
}

fn verify_sof3(data: &[u8], width: u32, height: u32) -> Result<(), Error> {
    if range(data, 0, 2)? != [0xff, 0xd8] {
        return Err(Error::Invalid);
    }
    let mut cursor = 2usize;
    loop {
        if *range(data, cursor, 1)?.first().ok_or(Error::Incomplete)? != 0xff {
            return Err(Error::Invalid);
        }
        while *range(data, cursor, 1)?.first().ok_or(Error::Incomplete)? == 0xff {
            cursor = cursor.checked_add(1).ok_or(Error::Invalid)?;
        }
        let marker = data[cursor];
        cursor = cursor.checked_add(1).ok_or(Error::Invalid)?;
        if marker == 0xc3 {
            let segment = range(data, cursor, 17)?;
            if u16::from_be_bytes([segment[0], segment[1]]) != 17
                || segment[2] != 8
                || segment[7] != 3
                || u32::from(u16::from_be_bytes([segment[3], segment[4]])) != height
                || u32::from(u16::from_be_bytes([segment[5], segment[6]])) != width
            {
                return Err(Error::Unsupported);
            }
            return Ok(());
        }
        if matches!(marker, 0xc0..=0xc2 | 0xc5..=0xc7 | 0xc9..=0xcb | 0xcd..=0xcf) {
            return Err(Error::Unsupported);
        }
        if matches!(marker, 0xd8 | 0xd9 | 0xda | 0x00 | 0xd0..=0xd7 | 0x01) {
            return Err(Error::Invalid);
        }
        let length = range(data, cursor, 2)?;
        let size = u16::from_be_bytes([length[0], length[1]]) as usize;
        if size < 2 {
            return Err(Error::Invalid);
        }
        range(data, cursor, size)?;
        cursor = cursor.checked_add(size).ok_or(Error::Invalid)?;
    }
}

impl RgbImage {
    pub fn parse(
        data: &[u8],
        mut validate_jpeg: impl FnMut(&[u8], u32, u32) -> Result<(), Error>,
        mut decode_jpeg: impl FnMut(&[u8], u32, u32, u32, u32, &mut [u8]) -> Result<(), Error>,
    ) -> Result<Option<Self>, Error> {
        let header = range(data, 0, 8)?;
        let order = match &header[..2] {
            b"II" => ByteOrder::Little,
            b"MM" => ByteOrder::Big,
            _ => return Err(Error::Invalid),
        };
        if order.u16(&header[2..4]) != 42 {
            return Err(Error::Unsupported);
        }

        let Some((ifd, main_ifd_index, root_stage2_supported, root_stage3_supported)) =
            select_raw_ifd(data, order)?
        else {
            return Ok(None);
        };
        let tags = &ifd.tags;
        if main_ifd_index != 0 {
            for tag in tags {
                let valid = match tag.id {
                    254 => tag.kind == 4,
                    256 | 257 | 322 | 323 | 324 | 325 | 50717 | 50829 => {
                        tag.kind == 3 || tag.kind == 4
                    }
                    258 | 259 | 262 | 274 | 277 | 284 | 50713 => tag.kind == 3,
                    50714 | 50719 | 50720 => matches!(tag.kind, 3 | 4 | 5),
                    50718 => tag.kind == 5,
                    50738 | 50780 | 51009 => true, // Stage 2 validates these independently.
                    51022 => true, // Stage 3 rejects this until OpcodeList3 is implemented.
                    _ => return Err(Error::Unsupported),
                };
                if !valid {
                    return Err(Error::Invalid);
                }
            }
        }
        let width = scalar(required(tags, 256)?, order)?;
        let height = scalar(required(tags, 257)?, order)?;
        if width == 0 || height == 0 || width > 300_000 || height > 300_000 {
            return Err(Error::Invalid);
        }
        for (id, expected) in [
            (254, 0),
            (259, if main_ifd_index == 0 { 7 } else { 34892 }),
            (262, 34892),
            (277, 3),
        ] {
            if scalar(required(tags, id)?, order)? != expected {
                return Err(Error::Unsupported);
            }
        }
        if let Some(planar) = optional(tags, 284) {
            if scalar(planar, order)? != 1 {
                return Err(Error::Unsupported);
            }
        }
        if let Some(origin) = optional(tags, 274) {
            if scalar(origin, order)? != 1 {
                return Err(Error::Unsupported);
            }
        }
        let bits = required(tags, 258)?;
        if bits.count != 3 || (0..3).any(|i| number(bits, i, order) != Ok(8)) {
            return Err(Error::Unsupported);
        }
        identity_array(tags, 50713, &[1, 1], order)?;
        identity_array(tags, 50718, &[1, 1], order)?;
        identity_array(tags, 50719, &[0, 0], order)?;
        identity_array(tags, 50720, &[width, height], order)?;
        identity_array(tags, 50829, &[0, 0, height, width], order)?;
        if let Some(black) = optional(tags, 50714) {
            if black.count != 3 || (0..3).any(|i| integral(black, i, order) != Ok(0)) {
                return Err(Error::Unsupported);
            }
        }
        if let Some(white) = optional(tags, 50717) {
            if white.count != 3 || (0..3).any(|i| number(white, i, order) != Ok(255)) {
                return Err(Error::Unsupported);
            }
        }
        let stage2 = parse_stage2(tags, order, width, height, root_stage2_supported);
        let tile_width = scalar(required(tags, 322)?, order)?;
        let tile_height = scalar(required(tags, 323)?, order)?;
        if tile_width == 0 || tile_height == 0 || tile_width > 300_000 || tile_height > 300_000 {
            return Err(Error::Invalid);
        }
        if tile_width > 65535 || tile_height > 65535 {
            return Err(Error::Unsupported);
        }
        let columns = 1 + (width - 1) / tile_width;
        let rows = 1 + (height - 1) / tile_height;
        let count = columns.checked_mul(rows).ok_or(Error::Invalid)?;
        let offsets = required(tags, 324)?;
        let sizes = required(tags, 325)?;
        if offsets.count != count || sizes.count != count {
            return Err(Error::Invalid);
        }
        let output_len = (width as usize)
            .checked_mul(height as usize)
            .and_then(|n| n.checked_mul(3))
            .ok_or(Error::Invalid)?;
        for index in 0..count as usize {
            let (x, y, visible_width, visible_height, len) = visible_tile(
                width,
                height,
                tile_width,
                tile_height,
                (index as u32) % columns,
                (index as u32) / columns,
            )?;
            let visible_row = visible_width.checked_mul(3).ok_or(Error::Invalid)?;
            let end = y
                .checked_add(visible_height - 1)
                .and_then(|v| v.checked_mul(width as usize))
                .and_then(|v| v.checked_add(x))
                .and_then(|v| v.checked_mul(3))
                .and_then(|v| v.checked_add(visible_row))
                .ok_or(Error::Invalid)?;
            if end > output_len || len > output_len {
                return Err(Error::Invalid);
            }
            let offset = number(offsets, index, order)? as usize;
            let size = number(sizes, index, order)? as usize;
            if size == 0 {
                return Err(Error::Invalid);
            }
            range(data, offset, size)?;
        }
        for index in 0..count as usize {
            let offset = number(offsets, index, order)? as usize;
            let size = number(sizes, index, order)? as usize;
            let jpeg = range(data, offset, size)?;
            if main_ifd_index == 0 {
                verify_sof3(jpeg, tile_width, tile_height)?;
            }
            validate_jpeg(jpeg, tile_width, tile_height)?;
        }
        let mut pixels = Vec::new();
        pixels
            .try_reserve_exact(output_len)
            .map_err(|_| Error::OutOfMemory)?;
        pixels.resize(output_len, 0);
        let mut tile = Vec::new();
        for ty in 0..rows {
            for tx in 0..columns {
                let index = ty
                    .checked_mul(columns)
                    .and_then(|v| v.checked_add(tx))
                    .ok_or(Error::Invalid)? as usize;
                let offset = number(offsets, index, order)? as usize;
                let size = number(sizes, index, order)? as usize;
                let jpeg = range(data, offset, size)?;
                let (x, y, copy_width, copy_height, visible_len) =
                    visible_tile(width, height, tile_width, tile_height, tx, ty)?;
                if tile.len() < visible_len {
                    tile.try_reserve(visible_len - tile.len())
                        .map_err(|_| Error::OutOfMemory)?;
                }
                tile.resize(visible_len, 0);
                decode_jpeg(
                    jpeg,
                    tile_width,
                    tile_height,
                    copy_width as u32,
                    copy_height as u32,
                    &mut tile,
                )?;
                for row in 0..copy_height {
                    let from = row
                        .checked_mul(copy_width)
                        .and_then(|v| v.checked_mul(3))
                        .ok_or(Error::Invalid)?;
                    let to = y
                        .checked_add(row)
                        .and_then(|v| v.checked_mul(width as usize))
                        .and_then(|v| v.checked_add(x))
                        .ok_or(Error::Invalid)?
                        .checked_mul(3)
                        .ok_or(Error::Invalid)?;
                    let size = copy_width.checked_mul(3).ok_or(Error::Invalid)?;
                    let source = tile
                        .get(from..from.checked_add(size).ok_or(Error::Invalid)?)
                        .ok_or(Error::Invalid)?;
                    let dest = pixels
                        .get_mut(to..to.checked_add(size).ok_or(Error::Invalid)?)
                        .ok_or(Error::Invalid)?;
                    dest.copy_from_slice(source);
                }
            }
        }
        Ok(Some(Self {
            pixels,
            stage2,
            stage3_supported: root_stage3_supported && optional(tags, 51022).is_none(),
            width,
            height,
            main_ifd_index,
        }))
    }

    pub fn row(&self, row: u32, output: &mut [u8]) -> Result<(), Error> {
        if row >= self.height || output.len() != self.width as usize * 3 {
            return Err(Error::Invalid);
        }
        let stride = (self.width as usize).checked_mul(3).ok_or(Error::Invalid)?;
        let start = (row as usize).checked_mul(stride).ok_or(Error::Invalid)?;
        output.copy_from_slice(range(&self.pixels, start, stride)?);
        Ok(())
    }

    pub fn stage2_status(&self) -> Result<(), Error> {
        self.stage2.as_ref().map(|_| ()).map_err(|&error| error)
    }

    pub fn stage3_status(&self) -> Result<(), Error> {
        if !self.stage3_supported {
            return Err(Error::Unsupported);
        }
        self.stage2_status()
    }

    pub fn stage3_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        self.stage3_status()?;
        self.stage2_row(row, output)
    }

    pub fn stage2_row(&self, row: u32, output: &mut [u16]) -> Result<(), Error> {
        let plan = self.stage2.as_ref().map_err(|&error| error)?;
        let stride = (self.width as usize).checked_mul(3).ok_or(Error::Invalid)?;
        if row >= self.height || output.len() != stride {
            return Err(Error::Invalid);
        }
        let start = (row as usize).checked_mul(stride).ok_or(Error::Invalid)?;
        let source = range(&self.pixels, start, stride)?;
        let mut samples = Vec::new();
        samples
            .try_reserve_exact(stride)
            .map_err(|_| Error::OutOfMemory)?;
        samples.extend(source.iter().map(|&pixel| u16::from(pixel) * 257));
        plan.apply_row(row, &mut samples)
            .map_err(|error| match error {
                opcode::Error::Invalid => Error::Invalid,
                opcode::Error::Unsupported => Error::Unsupported,
                opcode::Error::OutOfMemory => Error::OutOfMemory,
            })?;
        output.copy_from_slice(&samples);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{root_stage3_metadata_supported, verify_sof3, ByteOrder, Error, RgbImage, Tag};
    use crate::dng::Image;

    type Field = (u16, u16, u32, Vec<u8>);

    fn ifd(data: &mut Vec<u8>, mut fields: Vec<Field>) -> (usize, Vec<(u16, usize)>) {
        fields.sort_unstable_by_key(|field| field.0);
        let start = data.len();
        data.extend_from_slice(&(fields.len() as u16).to_le_bytes());
        data.resize(data.len() + fields.len() * 12 + 4, 0);
        let mut entries = Vec::new();
        for (index, (id, kind, count, value)) in fields.into_iter().enumerate() {
            let entry = start + 2 + index * 12;
            data[entry..entry + 2].copy_from_slice(&id.to_le_bytes());
            data[entry + 2..entry + 4].copy_from_slice(&kind.to_le_bytes());
            data[entry + 4..entry + 8].copy_from_slice(&count.to_le_bytes());
            if value.len() <= 4 {
                data[entry + 8..entry + 8 + value.len()].copy_from_slice(&value);
            } else {
                if data.len() % 2 != 0 {
                    data.push(0);
                }
                let offset = data.len() as u32;
                data[entry + 8..entry + 12].copy_from_slice(&offset.to_le_bytes());
                data.extend_from_slice(&value);
            }
            entries.push((id, entry));
        }
        if data.len() % 2 != 0 {
            data.push(0);
        }
        (start, entries)
    }

    fn entry(entries: &[(u16, usize)], id: u16) -> usize {
        entries.iter().find(|&&(tag, _)| tag == id).expect("tag").1
    }

    fn child_entry(data: &[u8], child: usize, id: u16) -> usize {
        let count = u16::from_le_bytes(data[child..child + 2].try_into().expect("IFD count"));
        (0..count as usize)
            .map(|index| child + 2 + index * 12)
            .find(|&offset| {
                u16::from_le_bytes(data[offset..offset + 2].try_into().expect("tag id")) == id
            })
            .expect("child tag")
    }

    fn append_opcode3_ifd(data: &mut Vec<u8>, original: usize) -> u32 {
        let count = u16::from_le_bytes(data[original..original + 2].try_into().expect("IFD"));
        let entries = data[original + 2..original + 2 + count as usize * 12].to_vec();
        if data.len() % 2 != 0 {
            data.push(0);
        }
        let new_offset = data.len() as u32;
        data.extend_from_slice(&(count + 1).to_le_bytes());
        data.extend_from_slice(&entries);
        data.extend_from_slice(&51022u16.to_le_bytes());
        data.extend_from_slice(&7u16.to_le_bytes());
        data.extend_from_slice(&4u32.to_le_bytes());
        data.extend_from_slice(&[0; 4]);
        data.extend_from_slice(&0u32.to_le_bytes());
        new_offset
    }

    fn fixture() -> (Vec<u8>, usize, usize, usize) {
        let mut data = b"II\x2a\0\x08\0\0\0".to_vec();
        let (_, root) = ifd(
            &mut data,
            vec![
                (254, 4, 1, 1u32.to_le_bytes().to_vec()),
                (262, 3, 1, 6u16.to_le_bytes().to_vec()),
                (330, 4, 1, 0u32.to_le_bytes().to_vec()),
                (50706, 1, 4, vec![1, 4, 0, 0]),
                (50707, 1, 4, vec![1, 4, 0, 0]),
                (50708, 2, 5, b"test\0".to_vec()),
            ],
        );
        let root_subifd = entry(&root, 330) + 8;
        let (child_offset, child) = ifd(
            &mut data,
            vec![
                (254, 4, 1, 0u32.to_le_bytes().to_vec()),
                (256, 4, 1, 3u32.to_le_bytes().to_vec()),
                (257, 4, 1, 2u32.to_le_bytes().to_vec()),
                (258, 3, 3, [8u16.to_le_bytes(); 3].concat()),
                (259, 3, 1, 34892u16.to_le_bytes().to_vec()),
                (262, 3, 1, 34892u16.to_le_bytes().to_vec()),
                (277, 3, 1, 3u16.to_le_bytes().to_vec()),
                (322, 4, 1, 2u32.to_le_bytes().to_vec()),
                (323, 4, 1, 2u32.to_le_bytes().to_vec()),
                (324, 4, 2, vec![0; 8]),
                (325, 4, 2, [1u32.to_le_bytes(); 2].concat()),
                (51009, 7, 4, vec![0; 4]),
            ],
        );
        data[root_subifd..root_subifd + 4].copy_from_slice(&(child_offset as u32).to_le_bytes());
        let offset_entry = entry(&child, 324);
        let offsets = u32::from_le_bytes(
            data[offset_entry + 8..offset_entry + 12]
                .try_into()
                .expect("tile offset array"),
        ) as usize;
        for tile in 0..2 {
            if data.len() % 2 != 0 {
                data.push(0);
            }
            let offset = data.len() as u32;
            data[offsets + tile * 4..offsets + tile * 4 + 4].copy_from_slice(&offset.to_le_bytes());
            data.push(tile as u8 + 1);
        }
        (data, root_subifd, offsets, entry(&child, 51009))
    }

    fn root_sof3_fixture() -> (Vec<u8>, usize, usize) {
        let mut data = b"II\x2a\0\x08\0\0\0".to_vec();
        let rational = |value: u32| [value.to_le_bytes(), 1u32.to_le_bytes()].concat();
        let pair = |a: u32, b: u32| {
            let mut value = rational(a);
            value.extend_from_slice(&rational(b));
            value
        };
        let header = [
            0xff, 0xd8, 0xff, 0xc3, 0, 17, 8, 0, 2, 0, 2, 3, 1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0,
        ];
        let (_, tags) = ifd(
            &mut data,
            vec![
                (254, 4, 1, 0u32.to_le_bytes().to_vec()),
                (256, 4, 1, 3u32.to_le_bytes().to_vec()),
                (257, 4, 1, 2u32.to_le_bytes().to_vec()),
                (258, 3, 3, [8u16.to_le_bytes(); 3].concat()),
                (259, 3, 1, 7u16.to_le_bytes().to_vec()),
                (262, 3, 1, 34892u16.to_le_bytes().to_vec()),
                (274, 3, 1, 1u16.to_le_bytes().to_vec()),
                (277, 3, 1, 3u16.to_le_bytes().to_vec()),
                (284, 3, 1, 1u16.to_le_bytes().to_vec()),
                (322, 4, 1, 2u32.to_le_bytes().to_vec()),
                (323, 4, 1, 2u32.to_le_bytes().to_vec()),
                (324, 4, 2, vec![0; 8]),
                (325, 4, 2, [22u32.to_le_bytes(); 2].concat()),
                (50706, 1, 4, vec![1, 7, 0, 0]),
                (50707, 1, 4, vec![1, 3, 0, 0]),
                (50708, 2, 5, b"test\0".to_vec()),
                (50713, 3, 2, [1u16.to_le_bytes(); 2].concat()),
                (
                    50714,
                    5,
                    3,
                    [rational(0), rational(0), rational(0)].concat(),
                ),
                (50717, 3, 3, [255u16.to_le_bytes(); 3].concat()),
                (50718, 5, 2, pair(1, 1)),
                (50719, 5, 2, pair(0, 0)),
                (50720, 5, 2, pair(3, 2)),
                (50738, 5, 1, rational(1)),
                (50780, 5, 1, rational(1)),
                (51009, 7, 4, vec![0; 4]),
                (52550, 7, 4, vec![0; 4]),
            ],
        );
        let offsets_tag = entry(&tags, 324);
        let lengths_tag = entry(&tags, 325);
        let offsets = u32::from_le_bytes(
            data[offsets_tag + 8..offsets_tag + 12]
                .try_into()
                .expect("offset array"),
        ) as usize;
        for tile in 0..2 {
            let offset = data.len() as u32;
            data[offsets + tile * 4..offsets + tile * 4 + 4].copy_from_slice(&offset.to_le_bytes());
            data.extend_from_slice(&header);
            data.push(tile as u8 + 1);
        }
        (data, offsets, lengths_tag)
    }

    fn fake_decode(
        jpeg: &[u8],
        width: u32,
        height: u32,
        visible_width: u32,
        visible_height: u32,
        rgb: &mut [u8],
    ) -> Result<(), Error> {
        assert_eq!((width, height), (2, 2));
        assert_eq!(rgb.len(), (visible_width * visible_height * 3) as usize);
        rgb.fill(jpeg[0]);
        Ok(())
    }

    fn fake_validate(jpeg: &[u8], width: u32, height: u32) -> Result<(), Error> {
        assert_eq!((width, height), (2, 2));
        assert_eq!(jpeg.len(), 1);
        Ok(())
    }

    #[test]
    fn final_only_parent_metadata_preserves_stage3_when_valid() {
        let tone: Vec<u8> = [0f32, 0f32, 1f32, 1f32]
            .into_iter()
            .flat_map(f32::to_le_bytes)
            .collect();
        let accepts = |samples: &[u8], count: u32, kind: u16, black: u32| {
            let black_bytes = black.to_le_bytes();
            let tags = [
                Tag {
                    id: 50940,
                    kind,
                    count,
                    value: samples,
                },
                Tag {
                    id: 51110,
                    kind: 4,
                    count: 1,
                    value: &black_bytes,
                },
            ];
            root_stage3_metadata_supported(&tags, ByteOrder::Little)
        };
        assert!(accepts(&tone, 4, 11, 1));
        assert!(accepts(&tone, 4, 11, 0));
        assert!(!accepts(&tone, 4, 11, 2));
        assert!(!accepts(&tone, 4, 7, 1));
        assert!(!accepts(&tone[..12], 3, 11, 1));
        let mut broken = tone.clone();
        broken[8..12].copy_from_slice(&0f32.to_le_bytes());
        assert!(!accepts(&broken, 4, 11, 1));
        broken[8..12].copy_from_slice(&f32::NAN.to_le_bytes());
        assert!(!accepts(&broken, 4, 11, 1));
    }

    #[test]
    fn sof3_header_requires_matching_lossless_rgb_geometry() {
        let mut header = vec![
            0xff, 0xd8, 0xff, 0xc3, 0, 17, 8, 0, 2, 0, 2, 3, 1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0,
        ];
        assert_eq!(verify_sof3(&header, 2, 2), Ok(()));
        assert_eq!(verify_sof3(&header, 3, 2), Err(Error::Unsupported));
        header[3] = 0xc0;
        assert_eq!(verify_sof3(&header, 2, 2), Err(Error::Unsupported));
        header[3] = 0xc3;
        assert_eq!(verify_sof3(&header[..9], 2, 2), Err(Error::Incomplete));
        header[0] = 0;
        assert_eq!(verify_sof3(&header, 2, 2), Err(Error::Invalid));
    }

    #[test]
    fn checked_root_sof3_tiles_do_not_go_to_rgb16() {
        let (data, offsets, lengths_tag) = root_sof3_fixture();
        assert!(matches!(crate::rgb16::Plan::parse(&data), Ok(None)));
        let image = RgbImage::parse(
            &data,
            |jpeg, width, height| {
                assert_eq!((width, height, jpeg.len()), (2, 2, 22));
                Ok(())
            },
            |jpeg, _, _, visible_width, visible_height, output| {
                assert_eq!(output.len(), (visible_width * visible_height * 3) as usize);
                output.fill(*jpeg.last().expect("tile marker"));
                Ok(())
            },
        )
        .expect("checked root")
        .expect("SOF3 image");
        assert_eq!((image.main_ifd_index, image.width, image.height), (0, 3, 2));
        let mut row = [0u8; 9];
        image.row(1, &mut row).expect("cropped row");
        assert_eq!(row, [1, 1, 1, 1, 1, 1, 2, 2, 2]);
        assert_eq!(image.stage2_status(), Err(Error::Unsupported)); // Empty OpcodeList2.

        let mut bad_marker = data.clone();
        let first =
            u32::from_le_bytes(data[offsets..offsets + 4].try_into().expect("tile")) as usize;
        bad_marker[first + 3] = 0xc0;
        assert!(matches!(
            RgbImage::parse(
                &bad_marker,
                |_, _, _| panic!("SOF not validated"),
                |_, _, _, _, _, _| panic!("SOF not validated")
            ),
            Err(Error::Unsupported)
        ));
        let mut bad_offset = data.clone();
        bad_offset[offsets + 4..offsets + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(
                &bad_offset,
                |_, _, _| panic!("range not checked"),
                |_, _, _, _, _, _| panic!("range not checked")
            ),
            Err(Error::Incomplete)
        ));
        let mut bad_count = data;
        bad_count[lengths_tag + 4..lengths_tag + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(
                &bad_count,
                |_, _, _| panic!("count not checked"),
                |_, _, _, _, _, _| panic!("count not checked")
            ),
            Err(Error::Invalid)
        ));
    }

    #[test]
    fn selects_child_and_crops_tile_edges() {
        let (data, _, _, _) = fixture();
        let image = RgbImage::parse(&data, fake_validate, fake_decode)
            .expect("valid TIFF")
            .expect("raw SubIFD");
        assert_eq!((image.main_ifd_index, image.width, image.height), (1, 3, 2));
        let mut row = [0u8; 9];
        image.row(0, &mut row).expect("row zero");
        assert_eq!(row, [1, 1, 1, 1, 1, 1, 2, 2, 2]);
        image.row(1, &mut row).expect("row one");
        assert_eq!(row, [1, 1, 1, 1, 1, 1, 2, 2, 2]);
        assert_eq!(image.row(2, &mut row), Err(Error::Invalid));
        assert_eq!(image.row(0, &mut row[..8]), Err(Error::Invalid));
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        let mut untouched = [0xa5a5; 9];
        assert_eq!(image.stage2_row(0, &mut untouched), Err(Error::Unsupported));
        assert_eq!(untouched, [0xa5a5; 9]);
    }

    fn fixture_with_stage2() -> (Vec<u8>, usize, usize) {
        let (mut data, subifd, _, opcode_entry) = fixture();
        let mut payload = Vec::new();
        for word in [0u32, 0, 2, 3, 1, 1, 1, 1, 0] {
            payload.extend_from_slice(&word.to_be_bytes());
        }
        payload.extend_from_slice(&1.0f64.to_bits().to_be_bytes());
        let mut list = 1u32.to_be_bytes().to_vec();
        for word in [8u32, 0x01030000, 0, payload.len() as u32] {
            list.extend_from_slice(&word.to_be_bytes());
        }
        list.extend_from_slice(&payload);
        if data.len() % 2 != 0 {
            data.push(0);
        }
        let opcode_offset = data.len();
        data[opcode_entry + 4..opcode_entry + 8]
            .copy_from_slice(&(list.len() as u32).to_le_bytes());
        data[opcode_entry + 8..opcode_entry + 12]
            .copy_from_slice(&(opcode_offset as u32).to_le_bytes());
        data.extend_from_slice(&list);
        (data, subifd, opcode_offset)
    }

    #[test]
    fn stage2_plan_applies_only_after_complete_opcode_validation() {
        let (data, _, opcode_offset) = fixture_with_stage2();
        let image = RgbImage::parse(&data, fake_validate, fake_decode)
            .expect("valid TIFF")
            .expect("raw SubIFD");
        assert_eq!(image.stage2_status(), Ok(()));
        assert_eq!(image.stage3_status(), Ok(()));
        let mut stage1 = [0u8; 9];
        image.row(1, &mut stage1).expect("stage-1 row");
        assert_eq!(stage1, [1, 1, 1, 1, 1, 1, 2, 2, 2]);
        let mut stage2 = [0u16; 9];
        image.stage2_row(1, &mut stage2).expect("stage-2 row");
        assert_eq!(stage2, [257, 65535, 257, 257, 65535, 257, 514, 65535, 514]);
        let mut stage3 = [0u16; 9];
        image.stage3_row(1, &mut stage3).expect("identity stage 3");
        assert_eq!(stage3, stage2);
        let mut after = [0u8; 9];
        image.row(1, &mut after).expect("stage-1 preserved");
        assert_eq!(after, stage1);

        let mut unsupported = data.clone();
        unsupported[opcode_offset + 7] = 9;
        let image = RgbImage::parse(&unsupported, fake_validate, fake_decode)
            .expect("stage-1 still valid")
            .expect("raw SubIFD");
        assert_eq!(image.stage2_status(), Err(Error::Unsupported));
        let mut untouched = [0xa5a5; 9];
        assert_eq!(image.stage2_row(0, &mut untouched), Err(Error::Unsupported));
        assert_eq!(untouched, [0xa5a5; 9]);

        let mut malformed = data;
        malformed[opcode_offset..opcode_offset + 4].copy_from_slice(&u32::MAX.to_be_bytes());
        let image = RgbImage::parse(&malformed, fake_validate, fake_decode)
            .expect("stage-1 still valid")
            .expect("raw SubIFD");
        assert_eq!(image.stage2_status(), Err(Error::Invalid));
        assert_eq!(image.stage2_row(0, &mut untouched), Err(Error::Invalid));
        assert_eq!(untouched, [0xa5a5; 9]);
    }

    #[test]
    fn opcode_list3_in_root_or_raw_ifd_disables_only_stage3() {
        let (data, subifd, _) = fixture_with_stage2();
        let child = u32::from_le_bytes(data[subifd..subifd + 4].try_into().expect("child IFD"));
        for in_root in [false, true] {
            let mut modified = data.clone();
            if in_root {
                let new_root = append_opcode3_ifd(&mut modified, 8);
                modified[4..8].copy_from_slice(&new_root.to_le_bytes());
            } else {
                let new_child = append_opcode3_ifd(&mut modified, child as usize);
                modified[subifd..subifd + 4].copy_from_slice(&new_child.to_le_bytes());
            }
            let image = RgbImage::parse(&modified, fake_validate, fake_decode)
                .expect("valid stage-1 TIFF")
                .expect("raw SubIFD");
            assert_eq!(image.stage2_status(), Ok(()));
            assert_eq!(image.stage3_status(), Err(Error::Unsupported));
            let mut raw = [0u8; 9];
            image.row(0, &mut raw).expect("stage 1");
            assert_eq!(raw, [1, 1, 1, 1, 1, 1, 2, 2, 2]);
            let mut stage2 = [0u16; 9];
            image.stage2_row(0, &mut stage2).expect("stage 2");
            let mut untouched = [0xa5a5u16; 9];
            assert_eq!(image.stage3_row(0, &mut untouched), Err(Error::Unsupported));
            assert_eq!(untouched, [0xa5a5; 9]);
        }
    }

    #[test]
    fn rejects_cycles_offsets_opcodes_and_callback_errors() {
        let (data, subifd, offsets, opcode) = fixture();
        let mut cycle = data.clone();
        cycle[subifd..subifd + 4].copy_from_slice(&8u32.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&cycle, fake_validate, fake_decode),
            Err(Error::Invalid)
        ));

        let mut missing_ifd = data.clone();
        missing_ifd[subifd..subifd + 4].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&missing_ifd, fake_validate, fake_decode),
            Err(Error::Incomplete)
        ));

        let mut tile_outside = data.clone();
        tile_outside[offsets + 4..offsets + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&tile_outside, fake_validate, fake_decode),
            Err(Error::Incomplete)
        ));

        let child = u32::from_le_bytes(
            data[subifd..subifd + 4]
                .try_into()
                .expect("raw IFD pointer"),
        ) as usize;
        let mut truncated_tile = data.clone();
        let lengths_tag = child_entry(&truncated_tile, child, 325);
        let lengths = u32::from_le_bytes(
            truncated_tile[lengths_tag + 8..lengths_tag + 12]
                .try_into()
                .expect("tile lengths pointer"),
        ) as usize;
        truncated_tile[lengths + 4..lengths + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&truncated_tile, fake_validate, fake_decode),
            Err(Error::Incomplete)
        ));

        let mut stage1_opcode = data.clone();
        stage1_opcode[opcode..opcode + 2].copy_from_slice(&51008u16.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&stage1_opcode, fake_validate, fake_decode),
            Err(Error::Unsupported)
        ));

        assert!(matches!(
            RgbImage::parse(&data, fake_validate, |_, _, _, _, _, _| Err(Error::Invalid)),
            Err(Error::Invalid)
        ));

        let mut no_tiles = data.clone();
        let tile_offsets = child_entry(&no_tiles, child, 324);
        no_tiles[tile_offsets + 4..tile_offsets + 8].copy_from_slice(&1u32.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&no_tiles, fake_validate, fake_decode),
            Err(Error::Invalid)
        ));

        let mut zero_tile = data.clone();
        let tile_width = child_entry(&zero_tile, child, 322);
        zero_tile[tile_width + 8..tile_width + 12].copy_from_slice(&0u32.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&zero_tile, fake_validate, fake_decode),
            Err(Error::Invalid)
        ));

        let mut overflowing_grid = data;
        for id in [256, 257] {
            let dimension = child_entry(&overflowing_grid, child, id);
            overflowing_grid[dimension + 8..dimension + 12]
                .copy_from_slice(&300_000u32.to_le_bytes());
        }
        for id in [322, 323] {
            let dimension = child_entry(&overflowing_grid, child, id);
            overflowing_grid[dimension + 8..dimension + 12].copy_from_slice(&1u32.to_le_bytes());
        }
        assert!(matches!(
            RgbImage::parse(&overflowing_grid, fake_validate, fake_decode),
            Err(Error::Invalid)
        ));
    }

    #[test]
    fn non_tiled_ifd_defers_bad_field_types_to_strict_monochrome_parser() {
        for (id, kind, count) in [(273, 5, 1), (50706, 4, 4), (50713, 4, 2), (50829, 5, 4)] {
            let mut data = b"II\x2a\0\x08\0\0\0".to_vec();
            ifd(
                &mut data,
                vec![(id, kind, count, u32::MAX.to_le_bytes().to_vec())],
            );
            assert!(
                matches!(RgbImage::parse(&data, fake_validate, fake_decode), Ok(None)),
                "tag {id}"
            );
            assert!(
                matches!(Image::parse(data), Err(Error::Invalid)),
                "tag {id}"
            );
        }

        let (mut data, subifd, _, _) = fixture();
        data[subifd - 6..subifd - 4].copy_from_slice(&5u16.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(&data, fake_validate, fake_decode),
            Err(Error::Invalid)
        ));
    }

    #[test]
    fn tiny_visible_area_does_not_allocate_for_padded_or_missing_tiles() {
        let (mut data, subifd, offsets, _) = fixture();
        let child = u32::from_le_bytes(
            data[subifd..subifd + 4]
                .try_into()
                .expect("raw IFD pointer"),
        ) as usize;
        for id in [256, 257] {
            let at = child_entry(&data, child, id) + 8;
            data[at..at + 4].copy_from_slice(&1u32.to_le_bytes());
        }
        let width = child_entry(&data, child, 322) + 8;
        let height = child_entry(&data, child, 323) + 8;
        let offset_tag = child_entry(&data, child, 324);
        let size_tag = child_entry(&data, child, 325);
        data[offset_tag + 4..offset_tag + 8].copy_from_slice(&1u32.to_le_bytes());
        data[size_tag + 4..size_tag + 8].copy_from_slice(&1u32.to_le_bytes());
        data[size_tag + 8..size_tag + 12].copy_from_slice(&1u32.to_le_bytes());
        let first_tile = data[offsets..offsets + 4].to_vec();
        data[offset_tag + 8..offset_tag + 12].copy_from_slice(&first_tile);

        let mut beyond_jpeg = data.clone();
        beyond_jpeg[width..width + 4].copy_from_slice(&300_000u32.to_le_bytes());
        beyond_jpeg[height..height + 4].copy_from_slice(&300_000u32.to_le_bytes());
        let mut called = false;
        assert!(matches!(
            RgbImage::parse(
                &beyond_jpeg,
                |_, _, _| {
                    called = true;
                    Err(Error::Invalid)
                },
                |_, _, _, _, _, _| panic!("geometry not validated")
            ),
            Err(Error::Unsupported)
        ));
        assert!(!called);

        data[width..width + 4].copy_from_slice(&65535u32.to_le_bytes());
        data[height..height + 4].copy_from_slice(&65535u32.to_le_bytes());
        let mut validated = 0;
        let mut calls = 0;
        let image = RgbImage::parse(
            &data,
            |jpeg, encoded_w, encoded_h| {
                validated += 1;
                assert_eq!((encoded_w, encoded_h, jpeg.len()), (65535, 65535, 1));
                Ok(())
            },
            |jpeg, encoded_w, encoded_h, visible_w, visible_h, rgb| {
                calls += 1;
                assert_eq!(
                    (encoded_w, encoded_h, visible_w, visible_h),
                    (65535, 65535, 1, 1)
                );
                assert_eq!(rgb.len(), 3);
                rgb.fill(jpeg[0]);
                Ok(())
            },
        )
        .expect("checked padded geometry")
        .expect("raw child");
        assert_eq!(validated, 1);
        assert_eq!(calls, 1);
        let mut row = [0; 3];
        image.row(0, &mut row).expect("visible row");
        assert_eq!(row, [1, 1, 1]);

        let mut missing = data.clone();
        missing[offset_tag + 8..offset_tag + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(
                &missing,
                |_, _, _| panic!("range not validated"),
                |_, _, _, _, _, _| panic!("range not validated")
            ),
            Err(Error::Incomplete)
        ));

        let mut short = data.clone();
        short[size_tag + 8..size_tag + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            RgbImage::parse(
                &short,
                |_, _, _| panic!("range not validated"),
                |_, _, _, _, _, _| panic!("range not validated")
            ),
            Err(Error::Incomplete)
        ));

        let mut huge_visible = data;
        for id in [256, 257] {
            let at = child_entry(&huge_visible, child, id) + 8;
            huge_visible[at..at + 4].copy_from_slice(&65535u32.to_le_bytes());
        }
        let mut decoded = false;
        assert!(matches!(
            RgbImage::parse(
                &huge_visible,
                |jpeg, width, height| {
                    assert_eq!((jpeg.len(), width, height), (1, 65535, 65535));
                    Err(Error::Incomplete)
                },
                |_, _, _, _, _, _| {
                    decoded = true;
                    Ok(())
                }
            ),
            Err(Error::Incomplete)
        ));
        assert!(!decoded);
    }
}
