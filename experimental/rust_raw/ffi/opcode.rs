// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! The DNG 1.3 MapPolynomial opcode for normalized, interleaved 16-bit rows.
//! Other opcodes remain unsupported until their processing is implemented.

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    Unsupported,
    OutOfMemory,
}

struct Polynomial {
    top: u32,
    left: u32,
    bottom: u32,
    right: u32,
    plane: u32,
    planes: u32,
    row_pitch: u32,
    col_pitch: u32,
    coefficients: [f32; 9],
    degree: usize,
}

pub struct OpcodeList {
    width: u32,
    height: u32,
    channels: u32,
    operations: Vec<Polynomial>,
}

fn read_u32(data: &[u8], cursor: &mut usize) -> Result<u32, Error> {
    let end = cursor.checked_add(4).ok_or(Error::Invalid)?;
    let bytes: [u8; 4] = data
        .get(*cursor..end)
        .ok_or(Error::Invalid)?
        .try_into()
        .map_err(|_| Error::Invalid)?;
    *cursor = end;
    Ok(u32::from_be_bytes(bytes))
}

impl OpcodeList {
    pub fn is_empty(&self) -> bool {
        self.operations.is_empty()
    }

    pub fn parse(data: &[u8], width: u32, height: u32, channels: u32) -> Result<Self, Error> {
        if width == 0 || height == 0 || channels == 0 || channels > 5 {
            return Err(Error::Invalid);
        }
        let mut cursor = 0;
        let count = read_u32(data, &mut cursor)? as usize;
        if count > (data.len() - cursor) / 16 {
            return Err(Error::Invalid);
        }
        let mut operations = Vec::new();
        operations
            .try_reserve_exact(count)
            .map_err(|_| Error::OutOfMemory)?;
        for _ in 0..count {
            let id = read_u32(data, &mut cursor)?;
            let version = read_u32(data, &mut cursor)?;
            let flags = read_u32(data, &mut cursor)?;
            let size = read_u32(data, &mut cursor)? as usize;
            let end = cursor.checked_add(size).ok_or(Error::Invalid)?;
            let payload = data.get(cursor..end).ok_or(Error::Invalid)?;
            cursor = end;
            if id != 8 || !(0x01030000..=0x01070100).contains(&version) || flags != 0 {
                return Err(Error::Unsupported);
            }
            let mut position = 0;
            let top = read_u32(payload, &mut position)?;
            let left = read_u32(payload, &mut position)?;
            let bottom = read_u32(payload, &mut position)?;
            let right = read_u32(payload, &mut position)?;
            let plane = read_u32(payload, &mut position)?;
            let planes = read_u32(payload, &mut position)?;
            let row_pitch = read_u32(payload, &mut position)?;
            let col_pitch = read_u32(payload, &mut position)?;
            let degree = read_u32(payload, &mut position)? as usize;
            if top >= bottom
                || bottom > height
                || left >= right
                || right > width
                || planes == 0
                || plane.checked_add(planes).is_none_or(|end| end > channels)
                || row_pitch == 0
                || col_pitch == 0
                || degree > 8
                || (degree + 1)
                    .checked_mul(8)
                    .and_then(|bytes| position.checked_add(bytes))
                    != Some(payload.len())
            {
                return Err(Error::Invalid);
            }
            let mut coefficients = [0.0; 9];
            for coefficient in coefficients.iter_mut().take(degree + 1) {
                let end = position.checked_add(8).ok_or(Error::Invalid)?;
                let bytes: [u8; 8] = payload
                    .get(position..end)
                    .ok_or(Error::Invalid)?
                    .try_into()
                    .map_err(|_| Error::Invalid)?;
                position = end;
                let precise = f64::from_bits(u64::from_be_bytes(bytes));
                if !precise.is_finite() {
                    return Err(Error::Invalid);
                }
                *coefficient = precise as f32;
                if !coefficient.is_finite() {
                    return Err(Error::Unsupported);
                }
            }
            operations.push(Polynomial {
                top,
                left,
                bottom,
                right,
                plane,
                planes,
                row_pitch,
                col_pitch,
                coefficients,
                degree,
            });
        }
        if cursor != data.len() {
            return Err(Error::Invalid);
        }
        Ok(Self {
            width,
            height,
            channels,
            operations,
        })
    }

    pub fn apply_row(&self, row: u32, pixels: &mut [u16]) -> Result<(), Error> {
        let expected = (self.width as usize)
            .checked_mul(self.channels as usize)
            .ok_or(Error::Invalid)?;
        if row >= self.height || pixels.len() != expected {
            return Err(Error::Invalid);
        }
        if self.operations.is_empty() {
            return Ok(());
        }
        let mut transformed = Vec::new();
        transformed
            .try_reserve_exact(pixels.len())
            .map_err(|_| Error::OutOfMemory)?;
        transformed.extend_from_slice(pixels);
        for operation in &self.operations {
            if row < operation.top
                || row >= operation.bottom
                || (row - operation.top) % operation.row_pitch != 0
            {
                continue;
            }
            for column in (operation.left..operation.right).step_by(operation.col_pitch as usize) {
                for plane in operation.plane..operation.plane + operation.planes {
                    let index = (column as usize) * (self.channels as usize) + plane as usize;
                    let input = f32::from(transformed[index]) / f32::from(u16::MAX);
                    let mut output = operation.coefficients[operation.degree];
                    for coefficient in operation.coefficients[..operation.degree].iter().rev() {
                        output = output * input + coefficient;
                    }
                    if !output.is_finite() {
                        return Err(Error::Invalid);
                    }
                    transformed[index] =
                        (output.clamp(0.0, 1.0) * f32::from(u16::MAX)).round() as u16;
                }
            }
        }
        pixels.copy_from_slice(&transformed);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{Error, OpcodeList};

    const PAYLOAD_START: usize = 4 + 16;

    fn map_polynomial(plane: u32, coefficients: &[f64]) -> Vec<u8> {
        let mut payload = Vec::new();
        for word in [0, 0, 1, 1, plane, 1, 1, 1, (coefficients.len() - 1) as u32] {
            payload.extend_from_slice(&word.to_be_bytes());
        }
        for &value in coefficients {
            payload.extend_from_slice(&value.to_bits().to_be_bytes());
        }
        let mut list = 1u32.to_be_bytes().to_vec();
        for word in [8u32, 0x01030000, 0, payload.len() as u32] {
            list.extend_from_slice(&word.to_be_bytes());
        }
        list.extend_from_slice(&payload);
        list
    }

    #[test]
    fn maps_only_selected_plane_in_float32_horner_order() {
        let bytes = map_polynomial(
            1,
            &[
                0.0029449912260624093,
                0.06219386587319753,
                0.0,
                0.9329079880979629,
            ],
        );
        let list = OpcodeList::parse(&bytes, 1, 1, 3).expect("valid polynomial");
        let mut pixels = [0, 165 * 257, 65535];
        list.apply_row(0, &mut pixels).expect("mapped row");
        assert_eq!(pixels, [0, 19394, 65535]);
    }

    #[test]
    fn rejects_invalid_sizes_and_unsupported_operations() {
        let good = map_polynomial(0, &[0.0, 1.0]);
        assert!(OpcodeList::parse(&good, 1, 1, 3).is_ok());
        assert!(matches!(
            OpcodeList::parse(&good[..good.len() - 1], 1, 1, 3),
            Err(Error::Invalid)
        ));
        let mut bad_opcode = good.clone();
        bad_opcode[4..8].copy_from_slice(&9u32.to_be_bytes());
        assert!(matches!(
            OpcodeList::parse(&bad_opcode, 1, 1, 3),
            Err(Error::Unsupported)
        ));
        let mut zero_pitch = good.clone();
        zero_pitch[PAYLOAD_START + 24..PAYLOAD_START + 28].copy_from_slice(&0u32.to_be_bytes());
        assert!(matches!(
            OpcodeList::parse(&zero_pitch, 1, 1, 3),
            Err(Error::Invalid)
        ));
        assert!(OpcodeList::parse(&good, 1, 1, 3)
            .unwrap()
            .apply_row(1, &mut [0, 0, 0])
            .is_err());
    }

    #[test]
    fn validates_geometry_and_does_not_publish_partial_rows() {
        let mut malformed = map_polynomial(0, &[0.0, 1.0]);
        malformed[PAYLOAD_START + 16..PAYLOAD_START + 20].copy_from_slice(&u32::MAX.to_be_bytes());
        assert!(matches!(
            OpcodeList::parse(&malformed, 2, 1, 3),
            Err(Error::Invalid)
        ));

        let mut overflow = map_polynomial(0, &[f32::MAX as f64, f32::MAX as f64]);
        overflow[PAYLOAD_START + 12..PAYLOAD_START + 16].copy_from_slice(&2u32.to_be_bytes());
        let operations = OpcodeList::parse(&overflow, 2, 1, 3).expect("finite coefficients");
        let mut row = [0, 65535, 0, 65535, 0, 0];
        let before = row;
        assert_eq!(operations.apply_row(0, &mut row), Err(Error::Invalid));
        assert_eq!(row, before);

        let mut impossible = u32::MAX.to_be_bytes().to_vec();
        impossible.extend_from_slice(&[0; 16]);
        assert!(matches!(
            OpcodeList::parse(&impossible, 1, 1, 3),
            Err(Error::Invalid)
        ));
    }

    #[test]
    fn applies_only_selected_rows_columns_and_planes() {
        let mut bytes = map_polynomial(1, &[1.0]);
        for (offset, value) in [
            (PAYLOAD_START + 8, 3u32),
            (PAYLOAD_START + 12, 3),
            (PAYLOAD_START + 24, 2),
            (PAYLOAD_START + 28, 2),
        ] {
            bytes[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
        }
        let list = OpcodeList::parse(&bytes, 3, 3, 3).expect("valid pitched ROI");
        let mut row0 = [100; 9];
        list.apply_row(0, &mut row0).expect("row zero");
        assert_eq!(row0, [100, 65535, 100, 100, 100, 100, 100, 65535, 100]);
        let mut row1 = [100; 9];
        list.apply_row(1, &mut row1).expect("unselected row");
        assert_eq!(row1, [100; 9]);
        let mut row2 = [100; 9];
        list.apply_row(2, &mut row2).expect("row two");
        assert_eq!(row2, row0);
    }
}
