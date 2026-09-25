// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]
#![allow(dead_code)]

//! Test-only color diagnostic for a controlled version of sample_1mp.dng.
//! Reads independently decoded Rust Stage-3 samples, not public SkCodec pixels.

mod color;
mod dng;
mod linearization;
mod opcode;
mod tiled;
mod tone;

use color::{sdk_illuminant_temperature, DualIlluminantProfile, SdkColorTransform};
use dng::{optional, required, scalar, ByteOrder, Tag};
use std::{env, fs};
use tone::SdkToneTable;

fn srational(tag: &Tag<'_>, index: usize, order: ByteOrder) -> f64 {
    assert_eq!((tag.kind, tag.count as usize > index), (10, true));
    let pair = &tag.value[index * 8..index * 8 + 8];
    let num = order.u32(&pair[..4]) as i32;
    let den = order.u32(&pair[4..]) as i32;
    assert!(den > 0);
    f64::from(num) / f64::from(den)
}

fn urational(tag: &Tag<'_>, index: usize, order: ByteOrder) -> f64 {
    assert_eq!((tag.kind, tag.count as usize > index), (5, true));
    let pair = &tag.value[index * 8..index * 8 + 8];
    let num = order.u32(&pair[..4]);
    let den = order.u32(&pair[4..]);
    assert!(den > 0);
    f64::from(num) / f64::from(den)
}

fn matrix(tags: &[Tag<'_>], id: u16, order: ByteOrder) -> [[f64; 3]; 3] {
    let tag = required(tags, id).expect("matrix tag");
    assert_eq!((tag.kind, tag.count), (10, 9));
    std::array::from_fn(|row| std::array::from_fn(|col| srational(tag, row * 3 + col, order)))
}

fn profile(data: &[u8]) -> DualIlluminantProfile {
    assert_eq!(&data[..4], b"II\x2a\0");
    let order = ByteOrder::Little;
    let first = order.u32(&data[4..8]) as usize;
    let ifd = tiled::read_ifd(data, first, order).expect("validated root IFD");
    let tags = &ifd.tags;
    assert_eq!(scalar(required(tags, 256).unwrap(), order), Ok(256));
    assert_eq!(scalar(required(tags, 257).unwrap(), order), Ok(144));
    assert_eq!(scalar(required(tags, 51110).unwrap(), order), Ok(1));
    assert!(optional(tags, 50940).is_some_and(|tag| {
        tag.kind == 11
            && tag.count == 4
            && tag.value.chunks_exact(4).map(|part| order.u32(part)).eq([
                0,
                0,
                1f32.to_bits(),
                1f32.to_bits(),
            ])
    }));
    assert!(optional(tags, 52525).is_none() && optional(tags, 52544).is_none());
    let neutral = required(tags, 50728).expect("camera neutral");
    let analog = required(tags, 50727).expect("analog balance");
    assert_eq!((neutral.kind, neutral.count), (5, 3));
    assert_eq!((analog.kind, analog.count), (5, 3));
    DualIlluminantProfile {
        color_matrices: [matrix(tags, 50721, order), matrix(tags, 50722, order)],
        camera_calibrations: [matrix(tags, 50723, order), matrix(tags, 50724, order)],
        forward_matrices: [matrix(tags, 50964, order), matrix(tags, 50965, order)],
        analog_balance: std::array::from_fn(|i| urational(analog, i, order)),
        camera_neutral: std::array::from_fn(|i| urational(neutral, i, order)),
        illuminant_kelvin: std::array::from_fn(|i| {
            let tag = required(tags, [50778, 50779][i]).expect("calibration illuminant");
            assert_eq!((tag.kind, tag.count), (3, 1));
            sdk_illuminant_temperature(
                u16::try_from(scalar(tag, order).expect("illuminant value"))
                    .expect("SHORT illuminant"),
            )
            .expect("checked SDK illuminant")
        }),
    }
}

fn main() {
    let args: Vec<_> = env::args().collect();
    assert_eq!(
        args.len(),
        4,
        "usage: color_probe DNG STAGE3.rgb16 OUTPUT.rgb8"
    );
    let data = fs::read(&args[1]).expect("controlled Skia DNG");
    let input = fs::read(&args[2]).expect("independently decoded Rust Stage 3");
    assert_eq!(input.len(), 600 * 338 * 3 * 2);
    let transform =
        SdkColorTransform::from_dual_illuminant(&profile(&data)).expect("controlled SDR profile");
    let tone = SdkToneTable::from_function(Ok).expect("identity tone");
    // Adobe DNG SDK 1.7.1.2724,
    // source/dng_color_space.cpp::dng_function_GammaEncode_sRGB.
    // Copyright 2006-2019 Adobe Systems Incorporated. All Rights Reserved.
    // See experimental/rust_raw/licenses/LICENSE.adobe-dng-sdk and PROVENANCE.md.
    let gamma = SdkToneTable::from_function(|value| {
        Ok(if value <= 0.0031308 {
            value * 12.92
        } else {
            1.055 * value.powf(1.0 / 2.4) - 0.055
        })
    })
    .expect("final sRGB transfer");
    let mut output = Vec::new();
    output
        .try_reserve_exact(input.len() / 2)
        .expect("image-sized output");
    for sample in input.chunks_exact(6) {
        let pixel = [0, 2, 4].map(|index| u16::from_le_bytes([sample[index], sample[index + 1]]));
        output.extend_from_slice(
            &transform
                .render_identity_sdr_srgb8_u16(pixel, &tone, &gamma)
                .expect("checked SDK-style SDR pixel"),
        );
    }
    fs::write(&args[3], output).expect("test-only SDR diagnostic");
}
