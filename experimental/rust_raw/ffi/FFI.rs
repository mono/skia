// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Experimental DNG reader bridge. This is not a general RAW decoder.

#[cxx::bridge(namespace = "rust_raw")]
mod ffi {
    #[derive(Debug, Clone, Copy, PartialEq)]
    enum DecodeStatus {
        Success,
        Invalid,
        Unsupported,
        Incomplete,
        OutOfMemory,
    }

    unsafe extern "C++" {
        include!("rust/common/SkStreamAdapter.h");
        #[namespace = "rust::stream"]
        type SkStreamAdapter = skia_rust_common::SkStreamAdapter;

        include!("experimental/rust_raw/ffi/JpegTileDecoder.h");
        fn validate_jpeg_tile(jpeg: &[u8], width: u32, height: u32) -> u8;
        fn decode_jpeg_tile(
            jpeg: &[u8],
            width: u32,
            height: u32,
            visible_width: u32,
            visible_height: u32,
            rgb: &mut [u8],
        ) -> u8;
        fn validate_jpeg16_bayer_tile(jpeg: &[u8], width: u32, height: u32) -> u8;
        fn decode_jpeg16_bayer_tile(
            jpeg: &[u8],
            width: u32,
            height: u32,
            visible_width: u32,
            visible_height: u32,
            samples: &mut [u16],
        ) -> u8;

        include!("experimental/rust_raw/ffi/DeflateStripDecoder.h");
        fn validate_dng_deflate_strip(encoded: &[u8], expected_bytes: usize) -> u8;
        fn inflate_dng_strip(encoded: &[u8], decoded: &mut [u8]) -> u8;
    }

    extern "Rust" {
        fn is_dng(data: &[u8]) -> bool;
        fn new_reader(input: UniquePtr<SkStreamAdapter>) -> Box<Reader>;
        type Reader;
        fn status(self: &Reader) -> DecodeStatus;
        fn stage1_status(self: &Reader) -> DecodeStatus;
        fn stage2_status(self: &Reader) -> DecodeStatus;
        fn stage3_status(self: &Reader) -> DecodeStatus;
        fn width(self: &Reader) -> u32;
        fn height(self: &Reader) -> u32;
        fn bits_per_sample(self: &Reader) -> u16;
        fn channels(self: &Reader) -> u32;
        fn stage3_channels(self: &Reader) -> u32;
        fn main_ifd_index(self: &Reader) -> u32;
        fn read_stage1_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_stage1_bayer_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_stage2_bayer_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_stage3_bayer_rgb_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_stage1_rgb_row(self: &Reader, row: u32, output: &mut [u8]) -> DecodeStatus;
        fn read_stage1_rgb16_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_stage1_rgb_f32_row(self: &Reader, row: u32, output: &mut [f32]) -> DecodeStatus;
        fn read_stage2_rgb_f32_row(self: &Reader, row: u32, output: &mut [f32]) -> DecodeStatus;
        fn read_stage3_rgb_f32_row(self: &Reader, row: u32, output: &mut [f32]) -> DecodeStatus;
        fn read_stage2_rgb_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_stage3_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_stage3_rgb_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn read_normalized_row(self: &Reader, row: u32, output: &mut [u16]) -> DecodeStatus;
        fn copy_rgb_row(self: &Reader, row: u32, output: &mut [u8]) -> bool;
    }
}

pub use ffi::*;

mod bayer;
#[cfg(test)]
mod color;
mod deflate;
mod dng;
mod float32;
#[cfg(test)]
mod gain_map;
mod input;
mod linearization;
mod opcode;
mod rgb16;
mod rgb8;
mod tiled;
#[cfg(test)]
mod tone;

pub struct Reader {
    status: DecodeStatus,
    stage1_status: DecodeStatus,
    image: Option<dng::Image>,
    mono_deflate: Option<deflate::Image>,
    bayer: Option<bayer::Image>,
    rgb: Option<tiled::RgbImage>,
    rgb8: Option<rgb8::Image>,
    rgb16: Option<rgb16::Image>,
    rgb_float: Option<float32::Image>,
}

fn decoding_status(error: dng::Error) -> DecodeStatus {
    match error {
        dng::Error::Invalid => DecodeStatus::Invalid,
        dng::Error::Unsupported => DecodeStatus::Unsupported,
        dng::Error::Incomplete => DecodeStatus::Incomplete,
        dng::Error::OutOfMemory => DecodeStatus::OutOfMemory,
    }
}

pub fn is_dng(data: &[u8]) -> bool {
    dng::has_dng_version(data)
}

fn tile_result(code: u8) -> Result<(), dng::Error> {
    match code {
        0 => Ok(()),
        1 => Err(dng::Error::Invalid),
        2 => Err(dng::Error::Unsupported),
        3 => Err(dng::Error::Incomplete),
        4 => Err(dng::Error::OutOfMemory),
        _ => Err(dng::Error::Invalid),
    }
}

pub fn new_reader(mut input: cxx::UniquePtr<skia_rust_common::SkStreamAdapter>) -> Box<Reader> {
    let mut reader = Reader {
        status: DecodeStatus::Invalid,
        stage1_status: DecodeStatus::Invalid,
        image: None,
        mono_deflate: None,
        bayer: None,
        rgb: None,
        rgb8: None,
        rgb16: None,
        rgb_float: None,
    };
    // Seekable assets are not subject to the existing 100 MiB forward-only limit.
    let Some(mut adapter) = input.as_mut() else {
        return Box::new(reader);
    };
    let bytes = match input::read_input(&mut adapter, input::MAX_FORWARD_BYTES) {
        Ok(bytes) => bytes,
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    };
    match rgb16::Plan::parse(&bytes) {
        Ok(Some(plan)) => {
            reader.stage1_status = DecodeStatus::Success;
            reader.status = DecodeStatus::Unsupported;
            reader.rgb16 = Some(rgb16::Image::new(bytes, plan));
            return Box::new(reader);
        }
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    match rgb16::Plan::parse_deflate(&bytes) {
        Ok(Some(plan)) => match rgb16::Image::inflate(
            bytes,
            plan,
            |encoded, expected| tile_result(ffi::validate_dng_deflate_strip(encoded, expected)),
            |encoded, decoded| tile_result(ffi::inflate_dng_strip(encoded, decoded)),
        ) {
            Ok(image) => {
                reader.stage1_status = DecodeStatus::Success;
                reader.status = DecodeStatus::Unsupported;
                reader.rgb16 = Some(image);
                return Box::new(reader);
            }
            Err(error) => {
                reader.status = decoding_status(error);
                reader.stage1_status = reader.status;
                return Box::new(reader);
            }
        },
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    match float32::Plan::parse(&bytes) {
        Ok(Some(plan)) => {
            reader.stage1_status = DecodeStatus::Success;
            reader.status = DecodeStatus::Unsupported;
            reader.rgb_float = Some(float32::Image::new(bytes, plan));
            return Box::new(reader);
        }
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    match rgb8::Plan::parse(&bytes) {
        Ok(Some(plan)) => {
            let image = rgb8::Image::new(bytes, plan);
            reader.stage1_status = DecodeStatus::Success;
            reader.status = if image.supports_final_render() {
                DecodeStatus::Success
            } else {
                DecodeStatus::Unsupported
            };
            reader.rgb8 = Some(image);
            return Box::new(reader);
        }
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    match rgb8::Plan::parse_deflate(&bytes) {
        Ok(Some(plan)) => match rgb8::Image::inflate(
            bytes,
            plan,
            |encoded, expected| tile_result(ffi::validate_dng_deflate_strip(encoded, expected)),
            |encoded, decoded| tile_result(ffi::inflate_dng_strip(encoded, decoded)),
        ) {
            Ok(image) => {
                reader.stage1_status = DecodeStatus::Success;
                reader.status = if image.supports_final_render() {
                    DecodeStatus::Success
                } else {
                    DecodeStatus::Unsupported
                };
                reader.rgb8 = Some(image);
                return Box::new(reader);
            }
            Err(error) => {
                reader.status = decoding_status(error);
                reader.stage1_status = reader.status;
                return Box::new(reader);
            }
        },
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    let tiled_bayer = bayer::Image::parse_tiled(
        &bytes,
        |jpeg, width, height| tile_result(ffi::validate_jpeg16_bayer_tile(jpeg, width, height)),
        |jpeg, width, height, visible_width, visible_height, samples| {
            tile_result(ffi::decode_jpeg16_bayer_tile(
                jpeg,
                width,
                height,
                visible_width,
                visible_height,
                samples,
            ))
        },
    );
    match tiled_bayer {
        Ok(Some(image)) => {
            reader.stage1_status = DecodeStatus::Success;
            reader.status = DecodeStatus::Unsupported;
            reader.bayer = Some(image);
            return Box::new(reader);
        }
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    match bayer::Plan::parse(&bytes) {
        Ok(Some(plan)) => {
            reader.stage1_status = DecodeStatus::Success;
            reader.status = DecodeStatus::Unsupported;
            reader.bayer = Some(bayer::Image::new(bytes, plan));
            return Box::new(reader);
        }
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    match bayer::Plan::parse_deflate(&bytes) {
        Ok(Some(plan)) => match bayer::Image::inflate(
            bytes,
            plan,
            |encoded, expected| tile_result(ffi::validate_dng_deflate_strip(encoded, expected)),
            |encoded, decoded| tile_result(ffi::inflate_dng_strip(encoded, decoded)),
        ) {
            Ok(image) => {
                reader.stage1_status = DecodeStatus::Success;
                reader.status = DecodeStatus::Unsupported;
                reader.bayer = Some(image);
                return Box::new(reader);
            }
            Err(error) => {
                reader.status = decoding_status(error);
                reader.stage1_status = reader.status;
                return Box::new(reader);
            }
        },
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    match deflate::Image::parse(
        &bytes,
        |encoded, expected| tile_result(ffi::validate_dng_deflate_strip(encoded, expected)),
        |encoded, decoded| tile_result(ffi::inflate_dng_strip(encoded, decoded)),
    ) {
        Ok(Some(image)) => {
            reader.stage1_status = DecodeStatus::Success;
            reader.status = DecodeStatus::Unsupported;
            reader.mono_deflate = Some(image);
            return Box::new(reader);
        }
        Ok(None) => {}
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
            return Box::new(reader);
        }
    }
    let tiled = tiled::RgbImage::parse(
        &bytes,
        |jpeg, width, height| tile_result(ffi::validate_jpeg_tile(jpeg, width, height)),
        |jpeg, width, height, visible_width, visible_height, rgb| {
            tile_result(ffi::decode_jpeg_tile(
                jpeg,
                width,
                height,
                visible_width,
                visible_height,
                rgb,
            ))
        },
    );
    match tiled {
        Ok(Some(rgb)) => {
            reader.stage1_status = DecodeStatus::Success;
            reader.status = DecodeStatus::Unsupported;
            reader.rgb = Some(rgb);
        }
        Ok(None) => match dng::Image::parse(bytes) {
            Ok(image) => {
                reader.stage1_status = DecodeStatus::Success;
                reader.status = if image.supports_final_render() {
                    DecodeStatus::Success
                } else {
                    DecodeStatus::Unsupported
                };
                reader.image = Some(image);
            }
            Err(error) => {
                reader.status = decoding_status(error);
                reader.stage1_status = reader.status;
            }
        },
        Err(error) => {
            reader.status = decoding_status(error);
            reader.stage1_status = reader.status;
        }
    }
    Box::new(reader)
}

impl Reader {
    pub fn status(&self) -> DecodeStatus {
        self.status
    }

    pub fn stage1_status(&self) -> DecodeStatus {
        self.stage1_status
    }

    pub fn stage2_status(&self) -> DecodeStatus {
        if let Some(image) = &self.mono_deflate {
            return image
                .stage2_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.bayer {
            return image
                .stage2_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.rgb8 {
            return image
                .stage2_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.rgb_float {
            return image
                .stage2_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.rgb16 {
            return image
                .stage2_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.image {
            return image
                .stage2_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        match &self.rgb {
            Some(rgb) => rgb
                .stage2_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None => self.stage1_status,
        }
    }

    pub fn stage3_status(&self) -> DecodeStatus {
        if let Some(image) = &self.mono_deflate {
            return image
                .stage3_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.bayer {
            return image
                .stage3_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.rgb8 {
            return image
                .stage3_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.rgb_float {
            return image
                .stage3_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if let Some(image) = &self.rgb16 {
            return image
                .stage3_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        match (&self.rgb, &self.image) {
            (Some(rgb), _) => rgb
                .stage3_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            (None, Some(image)) => image
                .stage3_status()
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            (None, None) => self.stage1_status,
        }
    }

    pub fn width(&self) -> u32 {
        if let Some(image) = &self.mono_deflate {
            return image.width();
        }
        if let Some(image) = &self.bayer {
            return image.width();
        }
        if let Some(image) = &self.rgb8 {
            return image.width();
        }
        if let Some(image) = &self.rgb_float {
            return image.width();
        }
        if let Some(image) = &self.rgb16 {
            return image.width();
        }
        self.rgb.as_ref().map_or_else(
            || self.image.as_ref().map_or(0, |image| image.width),
            |rgb| rgb.width,
        )
    }

    pub fn height(&self) -> u32 {
        if let Some(image) = &self.mono_deflate {
            return image.height();
        }
        if let Some(image) = &self.bayer {
            return image.height();
        }
        if let Some(image) = &self.rgb8 {
            return image.height();
        }
        if let Some(image) = &self.rgb_float {
            return image.height();
        }
        if let Some(image) = &self.rgb16 {
            return image.height();
        }
        self.rgb.as_ref().map_or_else(
            || self.image.as_ref().map_or(0, |image| image.height),
            |rgb| rgb.height,
        )
    }

    pub fn bits_per_sample(&self) -> u16 {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            16
        } else if self.rgb8.is_some() {
            8
        } else if self.rgb_float.is_some() {
            32
        } else if self.rgb16.is_some() {
            16
        } else if self.rgb.is_some() {
            8
        } else {
            self.image.as_ref().map_or(0, dng::Image::bits_per_sample)
        }
    }

    pub fn channels(&self) -> u32 {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            1
        } else if self.rgb.is_some()
            || self.rgb8.is_some()
            || self.rgb16.is_some()
            || self.rgb_float.is_some()
        {
            3
        } else if self.image.is_some() {
            1
        } else {
            0
        }
    }

    pub fn stage3_channels(&self) -> u32 {
        if self.bayer.is_some() {
            3
        } else {
            self.channels()
        }
    }

    pub fn main_ifd_index(&self) -> u32 {
        if self.bayer.is_some() || self.rgb8.is_some() || self.rgb_float.is_some() {
            0
        } else if let Some(image) = &self.rgb16 {
            image.main_ifd_index()
        } else {
            self.rgb.as_ref().map_or(0, |rgb| rgb.main_ifd_index)
        }
    }

    pub fn read_stage1_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        if let Some(image) = &self.mono_deflate {
            return image
                .raw_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        match &self.image {
            Some(image) => image
                .raw_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.rgb.is_some()
                || self.bayer.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some()
                || self.rgb_float.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage1_bayer_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        match &self.bayer {
            Some(image) => image
                .raw_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some()
                || self.mono_deflate.is_some()
                || self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some()
                || self.rgb_float.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage2_bayer_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        match &self.bayer {
            Some(image) => image
                .stage2_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some()
                || self.mono_deflate.is_some()
                || self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some()
                || self.rgb_float.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage3_bayer_rgb_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        match &self.bayer {
            Some(image) => image
                .stage3_rgb_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some()
                || self.mono_deflate.is_some()
                || self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some()
                || self.rgb_float.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_normalized_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        if let Some(image) = &self.mono_deflate {
            return image
                .normalized_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        match &self.image {
            Some(image) => image
                .normalized_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.rgb.is_some()
                || self.bayer.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some()
                || self.rgb_float.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage1_rgb_row(&self, row: u32, output: &mut [u8]) -> DecodeStatus {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            return DecodeStatus::Unsupported;
        }
        if let Some(image) = &self.rgb8 {
            return image
                .raw_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        match &self.rgb {
            Some(rgb) => rgb
                .row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some() || self.rgb16.is_some() || self.rgb_float.is_some() => {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage1_rgb16_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            return DecodeStatus::Unsupported;
        }
        match &self.rgb16 {
            Some(image) => image
                .row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some()
                || self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb_float.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage1_rgb_f32_row(&self, row: u32, output: &mut [f32]) -> DecodeStatus {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            return DecodeStatus::Unsupported;
        }
        match &self.rgb_float {
            Some(image) => image
                .row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some()
                || self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage2_rgb_f32_row(&self, row: u32, output: &mut [f32]) -> DecodeStatus {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            return DecodeStatus::Unsupported;
        }
        match &self.rgb_float {
            Some(image) => image
                .stage2_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some()
                || self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage3_rgb_f32_row(&self, row: u32, output: &mut [f32]) -> DecodeStatus {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            return DecodeStatus::Unsupported;
        }
        match &self.rgb_float {
            Some(image) => image
                .stage3_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some()
                || self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage2_rgb_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            return DecodeStatus::Unsupported;
        }
        if let Some(image) = &self.rgb8 {
            return image
                .stage2_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if self.rgb_float.is_some() {
            return DecodeStatus::Unsupported;
        }
        if let Some(image) = &self.rgb16 {
            return image
                .stage2_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        match &self.rgb {
            Some(rgb) => rgb
                .stage2_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some() || self.rgb8.is_some() || self.rgb16.is_some() => {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage3_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        if let Some(image) = &self.mono_deflate {
            return image
                .stage3_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if self.bayer.is_some() {
            return DecodeStatus::Unsupported;
        }
        match &self.image {
            Some(image) => image
                .stage3_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.rgb.is_some()
                || self.rgb8.is_some()
                || self.rgb16.is_some()
                || self.rgb_float.is_some() =>
            {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn read_stage3_rgb_row(&self, row: u32, output: &mut [u16]) -> DecodeStatus {
        if self.bayer.is_some() || self.mono_deflate.is_some() {
            return DecodeStatus::Unsupported;
        }
        if let Some(image) = &self.rgb8 {
            return image
                .stage3_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        if self.rgb_float.is_some() {
            return DecodeStatus::Unsupported;
        }
        if let Some(image) = &self.rgb16 {
            return image
                .stage3_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success);
        }
        match &self.rgb {
            Some(rgb) => rgb
                .stage3_row(row, output)
                .map_or_else(decoding_status, |()| DecodeStatus::Success),
            None if self.image.is_some() || self.rgb8.is_some() || self.rgb16.is_some() => {
                DecodeStatus::Unsupported
            }
            None => self.stage1_status,
        }
    }

    pub fn copy_rgb_row(&self, row: u32, output: &mut [u8]) -> bool {
        if let Some(image) = &self.rgb8 {
            return image.copy_rgb_row(row, output).is_ok();
        }
        self.image
            .as_ref()
            .is_some_and(|image| image.copy_rgb_row(row, output))
    }
}
