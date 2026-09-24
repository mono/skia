// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Scalar 2x2 Bayer reconstruction from checked Stage-2 samples. The same
//! horizontal/vertical weighting is used at edges as in the interior.

use crate::dng::Error;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BayerPattern {
    Rggb,
    Bggr,
    Grbg,
    Gbrg,
}

impl BayerPattern {
    pub fn parse(bytes: &[u8]) -> Result<Self, Error> {
        match bytes {
            [0, 1, 1, 2] => Ok(Self::Rggb),
            [2, 1, 1, 0] => Ok(Self::Bggr),
            [1, 0, 2, 1] => Ok(Self::Grbg),
            [1, 2, 0, 1] => Ok(Self::Gbrg),
            _ => Err(Error::Unsupported),
        }
    }

    pub fn color(self, x: usize, y: usize) -> usize {
        let index = ((y & 1) << 1) | (x & 1);
        match self {
            Self::Rggb => [0, 1, 1, 2][index],
            Self::Bggr => [2, 1, 1, 0][index],
            Self::Grbg => [1, 0, 2, 1][index],
            Self::Gbrg => [1, 2, 0, 1][index],
        }
    }
}

const DIAGONAL: &[(isize, isize)] = &[(-1, -1), (1, -1), (-1, 1), (1, 1)];
const HORIZONTAL: &[(isize, isize)] = &[(-1, 0), (1, 0)];
const VERTICAL: &[(isize, isize)] = &[(0, -1), (0, 1)];

fn sum_neighbors(
    sample_at: &impl Fn(usize, usize) -> u16,
    width: usize,
    height: usize,
    x: usize,
    y: usize,
    offsets: &[(isize, isize)],
) -> (u32, u32) {
    let mut sum = 0u32;
    let mut count = 0u32;
    for &(dx, dy) in offsets {
        let Some(nx) = x.checked_add_signed(dx) else {
            continue;
        };
        let Some(ny) = y.checked_add_signed(dy) else {
            continue;
        };
        if nx < width && ny < height {
            sum += u32::from(sample_at(nx, ny));
            count += 1;
        }
    }
    (sum, count)
}

fn average(
    sample_at: &impl Fn(usize, usize) -> u16,
    width: usize,
    height: usize,
    x: usize,
    y: usize,
    offsets: &[(isize, isize)],
) -> u16 {
    let (sum, count) = sum_neighbors(sample_at, width, height, x, y, offsets);
    debug_assert!(count > 0);
    ((sum + count / 2) / count) as u16
}

fn green(
    sample_at: &impl Fn(usize, usize) -> u16,
    width: usize,
    height: usize,
    x: usize,
    y: usize,
) -> u16 {
    let (horizontal, h_count) = sum_neighbors(sample_at, width, height, x, y, HORIZONTAL);
    let (vertical, v_count) = sum_neighbors(sample_at, width, height, x, y, VERTICAL);
    debug_assert!(h_count > 0 && v_count > 0);
    let denominator = 2 * h_count * v_count;
    let numerator = horizontal * v_count + vertical * h_count;
    ((numerator + denominator / 2) / denominator) as u16
}

fn render_row(
    sample_at: &impl Fn(usize, usize) -> u16,
    width: usize,
    height: usize,
    y: usize,
    pattern: BayerPattern,
    output: &mut [u16],
) {
    for x in 0..width {
        let value = sample_at(x, y);
        let rgb = match pattern.color(x, y) {
            0 => [
                value,
                green(sample_at, width, height, x, y),
                average(sample_at, width, height, x, y, DIAGONAL),
            ],
            1 if pattern.color(x ^ 1, y) == 0 => [
                average(sample_at, width, height, x, y, HORIZONTAL),
                value,
                average(sample_at, width, height, x, y, VERTICAL),
            ],
            1 => [
                average(sample_at, width, height, x, y, VERTICAL),
                value,
                average(sample_at, width, height, x, y, HORIZONTAL),
            ],
            2 => [
                average(sample_at, width, height, x, y, DIAGONAL),
                green(sample_at, width, height, x, y),
                value,
            ],
            _ => unreachable!(),
        };
        output[3 * x..3 * x + 3].copy_from_slice(&rgb);
    }
}

#[allow(dead_code)] // The full-image version is a test oracle for the three-row decoder.
pub fn bilinear_row(
    samples: &[u16],
    width: usize,
    height: usize,
    y: usize,
    pattern: BayerPattern,
    output: &mut [u16],
) -> Result<(), Error> {
    if width < 2
        || height < 2
        || y >= height
        || samples.len() != width.checked_mul(height).ok_or(Error::Invalid)?
        || output.len() != width.checked_mul(3).ok_or(Error::Invalid)?
    {
        return Err(Error::Invalid);
    }
    render_row(
        &|x, y| samples[y * width + x],
        width,
        height,
        y,
        pattern,
        output,
    );
    Ok(())
}

pub fn bilinear_row_from_neighbors(
    previous: Option<&[u16]>,
    current: &[u16],
    next: Option<&[u16]>,
    width: usize,
    height: usize,
    y: usize,
    pattern: BayerPattern,
    output: &mut [u16],
) -> Result<(), Error> {
    if width < 2
        || height < 2
        || y >= height
        || current.len() != width
        || previous.is_some() != (y > 0)
        || next.is_some() != (y + 1 < height)
        || previous.is_some_and(|row| row.len() != width)
        || next.is_some_and(|row| row.len() != width)
        || output.len() != width.checked_mul(3).ok_or(Error::Invalid)?
    {
        return Err(Error::Invalid);
    }
    let previous = previous.unwrap_or(current);
    let next = next.unwrap_or(current);
    let sample_at = |x, row| {
        if row < y {
            previous[x]
        } else if row == y {
            current[x]
        } else {
            next[x]
        }
    };
    render_row(&sample_at, width, height, y, pattern, output);
    Ok(())
}

pub fn constant_row(rgb: [u16; 3], width: usize, output: &mut [u16]) -> Result<(), Error> {
    if width.checked_mul(3).ok_or(Error::Invalid)? != output.len() {
        return Err(Error::Invalid);
    }
    for pixel in output.chunks_exact_mut(3) {
        pixel.copy_from_slice(&rgb);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{bilinear_row, bilinear_row_from_neighbors, constant_row, BayerPattern, Error};

    #[test]
    fn constant_rggb_interpolates_exactly_including_edges() {
        let width = 16;
        let height = 16;
        let samples: Vec<u16> = (0..height)
            .flat_map(|y| {
                (0..width).map(move |x| match (y & 1, x & 1) {
                    (0, 0) => 10000,
                    (1, 1) => 30000,
                    _ => 20000,
                })
            })
            .collect();
        let mut bilinear = vec![0u16; width * 3];
        let mut exact = vec![0u16; width * 3];
        for y in 0..height {
            bilinear_row(
                &samples,
                width,
                height,
                y,
                BayerPattern::Rggb,
                &mut bilinear,
            )
            .expect("bilinear");
            constant_row([10000, 20000, 30000], width, &mut exact).expect("constant");
            assert_eq!(bilinear, exact);
        }
        let mut untouched = [0xa5a5; 3];
        assert_eq!(
            constant_row([1, 2, 3], 2, &mut untouched),
            Err(Error::Invalid)
        );
        assert_eq!(untouched, [0xa5a5; 3]);
    }

    #[test]
    fn varied_pattern_matches_border_weights() {
        let input = [
            256u16, 512, 0, 4095, 1692, 1729, 1766, 1803, 2284, 2321, 2358, 2395, 2876, 2913, 2950,
            2987,
        ];
        let mut output = [0u16; 12];
        bilinear_row(&input, 4, 4, 0, BayerPattern::Rggb, &mut output).expect("candidate row");
        assert_eq!(output[0], 256);
        assert_eq!(output[1], 1102);
        assert_eq!(output[2], 1729);
        assert!(matches!(
            bilinear_row(&input, 1, 16, 0, BayerPattern::Rggb, &mut output),
            Err(Error::Invalid)
        ));
    }

    #[test]
    fn three_row_interpolation_weights_each_direction_equally_at_edges() {
        let pixels: [u16; 16] = [
            1000, 2000, 3000, 6000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 13000,
            14000, 15000,
        ];
        for y in 0..4 {
            let previous = (y > 0).then(|| &pixels[(y - 1) * 4..y * 4]);
            let current = &pixels[y * 4..(y + 1) * 4];
            let next = (y + 1 < 4).then(|| &pixels[(y + 1) * 4..(y + 2) * 4]);
            let mut full = [0u16; 12];
            let mut streaming = [0u16; 12];
            bilinear_row(&pixels, 4, 4, y, BayerPattern::Rggb, &mut full).expect("full input");
            bilinear_row_from_neighbors(
                previous,
                current,
                next,
                4,
                4,
                y,
                BayerPattern::Rggb,
                &mut streaming,
            )
            .expect("three rows");
            assert_eq!(streaming, full);
            if y == 0 {
                assert_eq!(streaming[2 * 3 + 1], 5000);
            }
            if y == 3 {
                assert_eq!(streaming[3 + 1], 11000);
            }
        }
        let mut untouched = [0xa5a5; 12];
        assert_eq!(
            bilinear_row_from_neighbors(
                None,
                &pixels[4..8],
                None,
                4,
                4,
                1,
                BayerPattern::Rggb,
                &mut untouched,
            ),
            Err(Error::Invalid)
        );
        assert_eq!(untouched, [0xa5a5; 12]);
    }

    #[test]
    fn every_bayer_phase_reconstructs_uniform_rgb_at_odd_edges() {
        for (pattern, bytes) in [
            (BayerPattern::Rggb, &[0, 1, 1, 2][..]),
            (BayerPattern::Bggr, &[2, 1, 1, 0]),
            (BayerPattern::Grbg, &[1, 0, 2, 1]),
            (BayerPattern::Gbrg, &[1, 2, 0, 1]),
        ] {
            assert_eq!(BayerPattern::parse(bytes), Ok(pattern));
            let width = 5;
            let height = 5;
            let samples: Vec<u16> = (0..height)
                .flat_map(|y| (0..width).map(move |x| [10000, 20000, 30000][pattern.color(x, y)]))
                .collect();
            for y in 0..height {
                let previous = (y > 0).then(|| &samples[(y - 1) * width..y * width]);
                let current = &samples[y * width..(y + 1) * width];
                let next = (y + 1 < height).then(|| &samples[(y + 1) * width..(y + 2) * width]);
                let mut full = [0u16; 15];
                let mut streamed = [0u16; 15];
                bilinear_row(&samples, width, height, y, pattern, &mut full).expect("full");
                bilinear_row_from_neighbors(
                    previous,
                    current,
                    next,
                    width,
                    height,
                    y,
                    pattern,
                    &mut streamed,
                )
                .expect("streamed");
                assert_eq!(full, streamed);
                for rgb in streamed.chunks_exact(3) {
                    assert_eq!(rgb, [10000, 20000, 30000]);
                }
            }
        }
        assert_eq!(BayerPattern::parse(&[0, 1, 2, 1]), Err(Error::Unsupported));
    }
}
