// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

use super::dng::Error;
use std::io::{Read, Seek, SeekFrom};

const READ_CHUNK_BYTES: usize = 16 * 1024;
pub(super) const MAX_FORWARD_BYTES: usize = 100 * 1024 * 1024;

pub(super) fn read_input(
    mut input: impl Read + Seek,
    max_forward_bytes: usize,
) -> Result<Vec<u8>, Error> {
    let length = match input.seek(SeekFrom::End(0)) {
        Ok(end) => {
            input
                .seek(SeekFrom::Start(0))
                .map_err(|_| Error::Unsupported)?;
            Some(usize::try_from(end).map_err(|_| Error::OutOfMemory)?)
        }
        Err(_) => None,
    };
    let mut bytes = Vec::new();
    loop {
        let remaining = if let Some(length) = length {
            length - bytes.len()
        } else {
            max_forward_bytes - bytes.len()
        };
        if remaining == 0 {
            if length.is_some() {
                break;
            }
            let mut extra = [0];
            match input.read(&mut extra) {
                Ok(0) => break,
                Ok(1) => return Err(Error::Invalid),
                _ => return Err(Error::Incomplete),
            }
        }
        let chunk = remaining.min(READ_CHUNK_BYTES);
        bytes.try_reserve(chunk).map_err(|_| Error::OutOfMemory)?;
        let start = bytes.len();
        bytes.resize(start + chunk, 0);
        let count = match input.read(&mut bytes[start..]) {
            Ok(count) if count <= chunk => count,
            _ => return Err(Error::Incomplete),
        };
        bytes.truncate(start + count);
        if count == 0 {
            return if length.is_some() {
                Err(Error::Incomplete)
            } else {
                Ok(bytes)
            };
        }
        if bytes.len() >= 8 {
            let header = &bytes[..4];
            if header != b"II\x2a\0" && header != b"MM\0\x2a" {
                return Err(if &header[..2] == b"II" || &header[..2] == b"MM" {
                    Error::Unsupported
                } else {
                    Error::Invalid
                });
            }
        }
    }
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::{read_input, Error, MAX_FORWARD_BYTES};
    use std::io::{Cursor, Read, Seek, SeekFrom};

    struct ForwardOnly<'a> {
        bytes: &'a [u8],
        position: usize,
    }

    impl Read for ForwardOnly<'_> {
        fn read(&mut self, output: &mut [u8]) -> std::io::Result<usize> {
            let count = output.len().min(3).min(self.bytes.len() - self.position);
            output[..count].copy_from_slice(&self.bytes[self.position..self.position + count]);
            self.position += count;
            Ok(count)
        }
    }

    impl Seek for ForwardOnly<'_> {
        fn seek(&mut self, _: SeekFrom) -> std::io::Result<u64> {
            Err(std::io::ErrorKind::Unsupported.into())
        }
    }

    struct InflatedLength<'a> {
        cursor: Cursor<&'a [u8]>,
    }

    impl Read for InflatedLength<'_> {
        fn read(&mut self, output: &mut [u8]) -> std::io::Result<usize> {
            self.cursor.read(output)
        }
    }

    impl Seek for InflatedLength<'_> {
        fn seek(&mut self, position: SeekFrom) -> std::io::Result<u64> {
            if let SeekFrom::End(0) = position {
                return Ok(self.cursor.get_ref().len() as u64 + 1);
            }
            self.cursor.seek(position)
        }
    }

    #[test]
    fn seekable_and_forward_only_inputs_preserve_bytes_and_limit() {
        assert_eq!(MAX_FORWARD_BYTES, 100 * 1024 * 1024);
        let bytes = b"II\x2a\0\x08\0\0\0followed by pixels";
        let mut seekable = Cursor::new(bytes);
        assert_eq!(read_input(&mut seekable, 8), Ok(bytes.to_vec()));

        let mut forward = ForwardOnly { bytes, position: 0 };
        assert_eq!(read_input(&mut forward, bytes.len()), Ok(bytes.to_vec()));
        assert_eq!(forward.position, bytes.len());

        let mut too_long = ForwardOnly { bytes, position: 0 };
        assert_eq!(
            read_input(&mut too_long, bytes.len() - 1),
            Err(Error::Invalid)
        );
        assert_eq!(too_long.position, bytes.len());
    }

    #[test]
    fn incomplete_known_length_and_unsupported_header_are_explicit() {
        let bytes = b"II\x2a\0\x08\0\0\0data";
        let mut missing = InflatedLength {
            cursor: Cursor::new(bytes.as_slice()),
        };
        assert_eq!(read_input(&mut missing, 8), Err(Error::Incomplete));

        let mut unsupported = ForwardOnly {
            bytes: b"II\x2b\0\0\0\0\0",
            position: 0,
        };
        assert_eq!(read_input(&mut unsupported, 32), Err(Error::Unsupported));
    }
}
