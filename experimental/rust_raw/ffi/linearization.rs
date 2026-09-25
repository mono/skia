// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

use super::dng::{number, ByteOrder, Error, Tag};

pub(crate) struct LinearizationTable {
    entries: Vec<u16>,
}

impl LinearizationTable {
    pub(crate) fn parse(tag: &Tag<'_>, order: ByteOrder) -> Result<Self, Error> {
        if tag.id != 50712 || tag.kind != 3 || tag.count == 0 || tag.count > u32::from(u16::MAX) + 1
        {
            return Err(Error::Invalid);
        }
        let count = tag.count as usize;
        let mut entries = Vec::new();
        entries
            .try_reserve_exact(count)
            .map_err(|_| Error::OutOfMemory)?;
        for index in 0..count {
            entries.push(u16::try_from(number(tag, index, order)?).map_err(|_| Error::Invalid)?);
        }
        Ok(Self { entries })
    }

    pub(crate) fn map(&self, sample: u16) -> u16 {
        self.entries[usize::from(sample).min(self.entries.len() - 1)]
    }

    pub(crate) fn has_identity_endpoints(&self) -> bool {
        self.entries[0] == 0 && self.entries[self.entries.len() - 1] == u16::MAX
    }

    pub(crate) fn maps_to_sdr_8_bit(&self) -> bool {
        self.entries[0] == 0
            && self.entries[self.entries.len() - 1] == u16::from(u8::MAX)
            && self
                .entries
                .iter()
                .all(|&value| value <= u16::from(u8::MAX))
    }
}

pub(crate) fn linearized_sample(table: Option<&LinearizationTable>, sample: u16) -> u16 {
    table.map_or(sample, |table| table.map(sample))
}

#[cfg(test)]
mod tests {
    use super::{ByteOrder, Error, LinearizationTable, Tag};

    #[test]
    fn maps_short_and_full_tables_in_either_byte_order() {
        for order in [ByteOrder::Little, ByteOrder::Big] {
            let encode = |sample: u16| match order {
                ByteOrder::Little => sample.to_le_bytes(),
                ByteOrder::Big => sample.to_be_bytes(),
            };
            let mut short = Vec::new();
            short.extend_from_slice(&encode(0));
            short.extend_from_slice(&encode(u16::MAX));
            let table = LinearizationTable::parse(
                &Tag {
                    id: 50712,
                    kind: 3,
                    count: 2,
                    value: &short,
                },
                order,
            )
            .expect("short table");
            assert!(table.has_identity_endpoints());
            assert_eq!(
                [table.map(0), table.map(1), table.map(65535)],
                [0, 65535, 65535]
            );

            let mut full = Vec::new();
            full.try_reserve_exact(65536 * 2).expect("test table");
            for sample in 0..=u16::MAX {
                full.extend_from_slice(&encode(sample.saturating_mul(2)));
            }
            let table = LinearizationTable::parse(
                &Tag {
                    id: 50712,
                    kind: 3,
                    count: 65536,
                    value: &full,
                },
                order,
            )
            .expect("full table");
            assert!(table.has_identity_endpoints());
            for sample in [0, 1, 256, 32767, 32768, u16::MAX] {
                assert_eq!(table.map(sample), sample.saturating_mul(2));
            }
        }
    }

    #[test]
    fn rejects_invalid_type_count_and_short_payload() {
        let invalid = |kind, count, value: &[u8]| {
            LinearizationTable::parse(
                &Tag {
                    id: 50712,
                    kind,
                    count,
                    value,
                },
                ByteOrder::Little,
            )
        };
        assert!(matches!(invalid(4, 1, &[0; 4]), Err(Error::Invalid)));
        assert!(matches!(invalid(3, 0, &[]), Err(Error::Invalid)));
        assert!(matches!(invalid(3, 2, &[0; 2]), Err(Error::Incomplete)));
    }

    #[test]
    fn rejects_tables_with_unaddressable_entries() {
        let mut bytes = vec![0; (65536 + 1) * 2];
        bytes[65535 * 2..65536 * 2].copy_from_slice(&255u16.to_le_bytes());
        bytes[65536 * 2..].copy_from_slice(&256u16.to_le_bytes());
        for (count, value) in [(65537, bytes.as_slice()), (u32::MAX, &[][..])] {
            assert!(matches!(
                LinearizationTable::parse(
                    &Tag {
                        id: 50712,
                        kind: 3,
                        count,
                        value,
                    },
                    ByteOrder::Little,
                ),
                Err(Error::Invalid)
            ));
        }
    }

    #[test]
    fn accepts_redundant_entries_within_limit() {
        let mut bytes = Vec::new();
        for sample in 0..=256u16 {
            bytes.extend_from_slice(&sample.saturating_mul(2).min(255).to_le_bytes());
        }
        let table = LinearizationTable::parse(
            &Tag {
                id: 50712,
                kind: 3,
                count: 257,
                value: &bytes,
            },
            ByteOrder::Little,
        )
        .expect("257-entry table");
        assert!(table.maps_to_sdr_8_bit());
        assert_eq!(
            [table.map(0), table.map(127), table.map(255)],
            [0, 254, 255]
        );
    }

    #[test]
    fn only_bounded_sdr_tables_enable_8bit_output() {
        for (values, valid) in [([0, 255], true), ([0, 256], false), ([1, 255], false)] {
            let bytes: Vec<u8> = values.into_iter().flat_map(u16::to_le_bytes).collect();
            let table = LinearizationTable::parse(
                &Tag {
                    id: 50712,
                    kind: 3,
                    count: 2,
                    value: &bytes,
                },
                ByteOrder::Little,
            )
            .expect("checked table");
            assert_eq!(table.maps_to_sdr_8_bit(), valid);
        }
        let values = [0u16, 256, 255];
        let bytes: Vec<u8> = values.into_iter().flat_map(u16::to_le_bytes).collect();
        let table = LinearizationTable::parse(
            &Tag {
                id: 50712,
                kind: 3,
                count: 3,
                value: &bytes,
            },
            ByteOrder::Little,
        )
        .expect("checked nonmonotone table");
        assert!(!table.maps_to_sdr_8_bit());
    }
}
