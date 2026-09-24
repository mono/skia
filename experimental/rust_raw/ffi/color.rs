// Copyright 2026 Google LLC.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![forbid(unsafe_code)]

//! Experimental, unregistered output-referred RGB color math. The remaining
//! one-byte SDK color differences must be resolved before this renders final pixels.

#[derive(Debug, PartialEq)]
pub(crate) enum ColorError {
    Invalid,
    Unsupported,
}

pub(crate) struct DngColorTransform {
    camera_to_xyz_d50: [[f64; 3]; 3],
    xyz_d50_to_srgb: [[f64; 3]; 3],
}

pub(crate) struct DualIlluminantProfile {
    pub color_matrices: [[[f64; 3]; 3]; 2],
    pub camera_calibrations: [[[f64; 3]; 3]; 2],
    pub forward_matrices: [[[f64; 3]; 3]; 2],
    pub analog_balance: [f64; 3],
    pub camera_neutral: [f64; 3],
    pub illuminant_kelvin: [f64; 2],
}

// Skia's canonical sRGB ICC profile matrix, expressed in XYZ D50.
const SRGB_TO_XYZ_D50: [[f64; 3]; 3] = [
    [0.436065674, 0.385147095, 0.143066406],
    [0.222488403, 0.716873169, 0.060607910],
    [0.013916016, 0.097076416, 0.714096069],
];

fn invert(matrix: &[[f64; 3]; 3]) -> Result<[[f64; 3]; 3], ColorError> {
    let [a, b, c] = matrix[0];
    let [d, e, f] = matrix[1];
    let [g, h, i] = matrix[2];
    let det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if !det.is_finite() || det == 0.0 {
        return Err(ColorError::Invalid);
    }
    let inverse = [
        [
            (e * i - f * h) / det,
            (c * h - b * i) / det,
            (b * f - c * e) / det,
        ],
        [
            (f * g - d * i) / det,
            (a * i - c * g) / det,
            (c * d - a * f) / det,
        ],
        [
            (d * h - e * g) / det,
            (b * g - a * h) / det,
            (a * e - b * d) / det,
        ],
    ];
    if inverse.iter().flatten().any(|value| !value.is_finite()) {
        return Err(ColorError::Invalid);
    }
    Ok(inverse)
}

fn multiply(left: &[[f64; 3]; 3], right: &[[f64; 3]; 3]) -> [[f64; 3]; 3] {
    std::array::from_fn(|row| {
        std::array::from_fn(|column| {
            (0..3)
                .map(|index| left[row][index] * right[index][column])
                .sum()
        })
    })
}

fn interpolate(matrices: &[[[f64; 3]; 3]; 2], weight: f64) -> [[f64; 3]; 3] {
    std::array::from_fn(|row| {
        std::array::from_fn(|column| {
            matrices[0][row][column] * (1.0 - weight) + matrices[1][row][column] * weight
        })
    })
}

fn diagonal(values: [f64; 3]) -> [[f64; 3]; 3] {
    std::array::from_fn(|row| {
        std::array::from_fn(|column| if row == column { values[row] } else { 0.0 })
    })
}

fn xyz_to_kelvin(xyz: [f64; 3]) -> Result<f64, ColorError> {
    if xyz.iter().any(|value| !value.is_finite() || *value <= 0.0) {
        return Err(ColorError::Invalid);
    }
    let sum: f64 = xyz.iter().sum();
    if !sum.is_finite() || sum <= 0.0 {
        return Err(ColorError::Invalid);
    }
    let x = xyz[0] / sum;
    let y = xyz[1] / sum;
    if !x.is_finite() || !y.is_finite() || x <= 0.0 || y <= 0.0 || x + y >= 1.0 || y == 0.1858 {
        return Err(ColorError::Unsupported);
    }
    // McCamy's public xy/CCT approximation; no DNG SDK interpolation code.
    let n = (x - 0.3320) / (y - 0.1858);
    let kelvin = -449.0 * n.powi(3) + 3525.0 * n.powi(2) - 6823.3 * n + 5520.33;
    if !kelvin.is_finite() || !(1667.0..=25000.0).contains(&kelvin) {
        return Err(ColorError::Unsupported);
    }
    Ok(kelvin)
}

impl DngColorTransform {
    pub(crate) fn from_forward_matrix(
        forward: [f64; 9],
        camera_neutral: [f64; 3],
    ) -> Result<Self, ColorError> {
        if camera_neutral
            .iter()
            .any(|value| !value.is_finite() || *value <= 0.0)
            || forward.iter().any(|value| !value.is_finite())
        {
            return Err(ColorError::Invalid);
        }
        if camera_neutral != [1.0; 3] {
            return Err(ColorError::Unsupported);
        }

        let mut camera_to_xyz_d50 = [[0.0; 3]; 3];
        for (row, output) in camera_to_xyz_d50.iter_mut().enumerate() {
            let source = &forward[3 * row..3 * row + 3];
            let source_white: f64 = source.iter().sum();
            let destination_white: f64 = SRGB_TO_XYZ_D50[row].iter().sum();
            if !source_white.is_finite() || source_white <= 0.0 {
                return Err(ColorError::Invalid);
            }
            let scale = destination_white / source_white;
            for channel in 0..3 {
                output[channel] = source[channel] * scale;
                if !output[channel].is_finite() {
                    return Err(ColorError::Invalid);
                }
            }
        }
        invert(&camera_to_xyz_d50)?;

        Ok(Self {
            camera_to_xyz_d50,
            xyz_d50_to_srgb: invert(&SRGB_TO_XYZ_D50)?,
        })
    }

    pub(crate) fn from_dual_illuminant(
        profile: &DualIlluminantProfile,
    ) -> Result<(Self, f64), ColorError> {
        Self::dual_transform(profile, false)
    }

    pub(crate) fn from_dual_illuminant_white_corrected(
        profile: &DualIlluminantProfile,
    ) -> Result<(Self, f64), ColorError> {
        Self::dual_transform(profile, true)
    }

    fn dual_transform(
        profile: &DualIlluminantProfile,
        correct_forward_white: bool,
    ) -> Result<(Self, f64), ColorError> {
        if profile
            .color_matrices
            .iter()
            .chain(&profile.camera_calibrations)
            .chain(&profile.forward_matrices)
            .flat_map(|matrix| matrix.iter().flatten())
            .any(|value| !value.is_finite())
            || profile
                .camera_neutral
                .iter()
                .chain(&profile.analog_balance)
                .any(|value| !value.is_finite() || *value <= 0.0)
            || profile
                .illuminant_kelvin
                .iter()
                .any(|value| !value.is_finite() || !(1667.0..=25000.0).contains(value))
            || profile.illuminant_kelvin[0] == profile.illuminant_kelvin[1]
        {
            return Err(ColorError::Invalid);
        }
        let balance = diagonal(profile.analog_balance);
        let mut weight: f64 = 0.5;
        let mut converged = false;
        for _ in 0..20 {
            let color = interpolate(&profile.color_matrices, weight);
            let calibration = interpolate(&profile.camera_calibrations, weight);
            let xyz_to_camera = multiply(&multiply(&balance, &calibration), &color);
            let camera_to_xyz = invert(&xyz_to_camera)?;
            let xyz = camera_to_xyz.map(|row| {
                row.iter()
                    .zip(profile.camera_neutral)
                    .map(|(value, neutral)| value * neutral)
                    .sum()
            });
            let kelvin = xyz_to_kelvin(xyz)?;
            let next = ((1.0 / kelvin - 1.0 / profile.illuminant_kelvin[0])
                / (1.0 / profile.illuminant_kelvin[1] - 1.0 / profile.illuminant_kelvin[0]))
                .clamp(0.0, 1.0);
            if (next - weight).abs() < 1e-12 {
                weight = next;
                converged = true;
                break;
            }
            weight = next;
        }
        if !converged {
            return Err(ColorError::Unsupported);
        }

        let calibration = interpolate(&profile.camera_calibrations, weight);
        let inverse_calibration = invert(&multiply(&balance, &calibration))?;
        let reference_neutral = inverse_calibration.map(|row| {
            row.iter()
                .zip(profile.camera_neutral)
                .map(|(value, neutral)| value * neutral)
                .sum()
        });
        if reference_neutral
            .iter()
            .any(|value: &f64| !value.is_finite() || *value <= 0.0)
        {
            return Err(ColorError::Invalid);
        }
        let mut forward = interpolate(&profile.forward_matrices, weight);
        for (row, components) in forward.iter_mut().enumerate() {
            let source_white: f64 = components.iter().sum();
            if !source_white.is_finite() || source_white <= 0.0 {
                return Err(ColorError::Invalid);
            }
            if correct_forward_white {
                let target_white: f64 = SRGB_TO_XYZ_D50[row].iter().sum();
                for component in components.iter_mut() {
                    *component *= target_white / source_white;
                }
            }
        }
        let adjustment = diagonal(reference_neutral.map(|value| 1.0 / value));
        let camera_to_xyz_d50 = multiply(&multiply(&forward, &adjustment), &inverse_calibration);
        invert(&camera_to_xyz_d50)?;
        Ok((
            Self {
                camera_to_xyz_d50,
                xyz_d50_to_srgb: invert(&SRGB_TO_XYZ_D50)?,
            },
            weight,
        ))
    }

    fn render_linear(&self, camera: [f64; 3]) -> Result<[u8; 3], ColorError> {
        let xyz: [f64; 3] = self.camera_to_xyz_d50.map(|row| {
            row.iter()
                .zip(camera)
                .map(|(value, sample)| value * sample)
                .sum()
        });
        let linear: [f64; 3] = self.xyz_d50_to_srgb.map(|row| {
            row.iter()
                .zip(xyz)
                .map(|(value, sample)| value * sample)
                .sum()
        });
        if linear.iter().any(|value: &f64| !value.is_finite()) {
            return Err(ColorError::Invalid);
        }
        Ok(linear.map(|value: f64| {
            let value = value.clamp(0.0, 1.0);
            let encoded = if value <= 0.0031308 {
                12.92 * value
            } else {
                1.055 * value.powf(1.0 / 2.4) - 0.055
            };
            (encoded * 255.0).round() as u8
        }))
    }

    pub(crate) fn render_srgb8(&self, samples: [u8; 3]) -> Result<[u8; 3], ColorError> {
        self.render_linear(samples.map(|sample| f64::from(sample) / 255.0))
    }

    pub(crate) fn render_srgb8_u16(&self, samples: [u16; 3]) -> Result<[u8; 3], ColorError> {
        self.render_linear(samples.map(|sample| f64::from(sample) / f64::from(u16::MAX)))
    }
}

#[cfg(test)]
mod tests {
    use super::{
        xyz_to_kelvin, ColorError, DngColorTransform, DualIlluminantProfile, SRGB_TO_XYZ_D50,
    };

    const FORWARD: [f64; 9] = [
        0.7978, 0.1352, 0.0313, 0.288, 0.7119, 0.0001, 0.0, 0.0, 0.8251,
    ];

    #[test]
    fn neutral_gray_follows_canonical_srgb() {
        let transform = DngColorTransform::from_forward_matrix(FORWARD, [1.0; 3]).unwrap();
        for sample in 0..=255u8 {
            let linear = f64::from(sample) / 255.0;
            let encoded = if linear <= 0.0031308 {
                12.92 * linear
            } else {
                1.055 * linear.powf(1.0 / 2.4) - 0.055
            };
            let byte = (encoded * 255.0).round() as u8;
            assert_eq!(transform.render_srgb8([sample; 3]), Ok([byte; 3]));
        }
    }

    #[test]
    fn color_uses_forward_matrix_not_separate_channel_gamma() {
        let transform = DngColorTransform::from_forward_matrix(FORWARD, [1.0; 3]).unwrap();
        let result = transform.render_srgb8([128, 0, 127]).unwrap();
        assert_eq!(result, [240, 0, 199]);
        assert_ne!(result, [188, 0, 187]);
    }

    #[test]
    fn invalid_and_unhandled_profiles_are_explicit() {
        assert!(matches!(
            DngColorTransform::from_forward_matrix([0.0; 9], [1.0; 3]),
            Err(ColorError::Invalid)
        ));
        assert!(matches!(
            DngColorTransform::from_forward_matrix([f64::NAN; 9], [1.0; 3]),
            Err(ColorError::Invalid)
        ));
        assert!(matches!(
            DngColorTransform::from_forward_matrix(FORWARD, [1.0, 0.8, 1.0]),
            Err(ColorError::Unsupported)
        ));
    }

    #[test]
    fn dual_illuminant_interpolation_preserves_neutral_srgb_and_rejects_bad_calibration() {
        let identity = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];
        let camera = [[1.037, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.212]];
        let mut profile = DualIlluminantProfile {
            color_matrices: [camera; 2],
            camera_calibrations: [identity; 2],
            forward_matrices: [SRGB_TO_XYZ_D50; 2],
            analog_balance: [1.0; 3],
            camera_neutral: [1.0; 3],
            illuminant_kelvin: [6500.0, 2856.0],
        };
        let (transform, weight) =
            DngColorTransform::from_dual_illuminant(&profile).expect("public CIE white balance");
        assert!((0.1..0.5).contains(&weight), "{weight}");
        let reference = DngColorTransform::from_forward_matrix(
            [
                SRGB_TO_XYZ_D50[0][0],
                SRGB_TO_XYZ_D50[0][1],
                SRGB_TO_XYZ_D50[0][2],
                SRGB_TO_XYZ_D50[1][0],
                SRGB_TO_XYZ_D50[1][1],
                SRGB_TO_XYZ_D50[1][2],
                SRGB_TO_XYZ_D50[2][0],
                SRGB_TO_XYZ_D50[2][1],
                SRGB_TO_XYZ_D50[2][2],
            ],
            [1.0; 3],
        )
        .expect("reference sRGB matrix");
        assert_eq!(
            transform.render_srgb8([128, 0, 127]),
            reference.render_srgb8([128, 0, 127])
        );
        assert_eq!(
            transform.render_srgb8_u16([u16::MAX, 0, u16::MAX]),
            Ok([255, 0, 255])
        );

        profile.illuminant_kelvin[1] = profile.illuminant_kelvin[0];
        assert!(matches!(
            DngColorTransform::from_dual_illuminant(&profile),
            Err(ColorError::Invalid)
        ));
        profile.illuminant_kelvin[1] = 2856.0;
        profile.color_matrices[0] = [[0.0; 3]; 3];
        profile.color_matrices[1] = [[0.0; 3]; 3];
        assert!(matches!(
            DngColorTransform::from_dual_illuminant(&profile),
            Err(ColorError::Invalid)
        ));
        assert_eq!(xyz_to_kelvin([1.0, 1.0, -0.25]), Err(ColorError::Invalid));
        assert_eq!(xyz_to_kelvin([-1.0, 1.0, 1.0]), Err(ColorError::Invalid));
    }

    #[test]
    fn interpolates_source_controlled_camera_profile_independently() {
        // Public DNG tags in resources/images/sample_1mp.dng, not decoder implementation data.
        let camera_calibration = [[0.9766, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 0.9922]];
        let mut profile = DualIlluminantProfile {
            color_matrices: [
                [
                    [0.6013, 0.0561, -0.1283],
                    [-0.6654, 1.5953, 0.0561],
                    [-0.2485, 0.5772, 0.4008],
                ],
                [
                    [1.0494, -0.2624, -0.2907],
                    [-0.4822, 1.4182, 0.0567],
                    [-0.0709, 0.2553, 0.5105],
                ],
            ],
            camera_calibrations: [camera_calibration; 2],
            forward_matrices: [
                [
                    [0.8203, -0.2188, 0.3594],
                    [0.3438, 0.5703, 0.0938],
                    [0.0156, -0.7266, 1.5391],
                ],
                [
                    [0.6797, -0.0781, 0.3594],
                    [0.2109, 0.7031, 0.0859],
                    [-0.0469, -0.8281, 1.6953],
                ],
            ],
            analog_balance: [1.0; 3],
            camera_neutral: [0.515625, 1.0, 0.65625],
            illuminant_kelvin: [6500.0, 2856.0],
        };
        let (transform, weight) = DngColorTransform::from_dual_illuminant(&profile)
            .expect("independent public-spec interpolation");
        assert!((weight - 0.23186826934440852).abs() < 1e-7, "{weight}");
        assert!((transform.camera_to_xyz_d50[0][0] - 1.527659289852).abs() < 1e-9);
        assert_eq!(
            transform.render_srgb8_u16([3604, 8659, 6823]),
            Ok([82, 103, 121])
        );
        let (corrected, corrected_weight) =
            DngColorTransform::from_dual_illuminant_white_corrected(&profile)
                .expect("measured compatibility candidate");
        assert!((corrected_weight - weight).abs() < 1e-12);
        assert_eq!(
            corrected.render_srgb8_u16([3604, 8659, 6823]),
            Ok([83, 102, 121])
        );
        assert_eq!(
            corrected.render_srgb8_u16([3765, 7478, 4070]),
            Ok([91, 96, 75])
        );
        profile.analog_balance[0] = 0.0;
        assert!(matches!(
            DngColorTransform::from_dual_illuminant(&profile),
            Err(ColorError::Invalid)
        ));
    }
}
