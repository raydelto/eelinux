/* SPDX-License-Identifier: MIT */
#ifndef PS2_NATIVE_WIDE_H
#define PS2_NATIVE_WIDE_H
#include "ps2_divide.h"
#include "ps2_multiply.h"
#include "ps2_unorm.h"
/* Validate a complete operation's borrowed float planes before allocation or
 * submission. Outputs may name inputs for explicitly in-place operations. */
int ps2_native_float_operands(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t *,unsigned,const uint32_t *,unsigned,uint32_t,unsigned,unsigned);
/* Float compositing before destination quantization. All 36 borrowed pages
 * must be distinct; source, mask and temporary contents may be consumed.
 * Only GS operations inspect pixel values. Basic operators use packed_blend. */
int ps2_native_float_blend_consume(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[8],const uint32_t mask[8],const uint32_t destination[8],
    const uint32_t temporary[12],uint32_t palette,unsigned width,unsigned height,
    unsigned operation,unsigned component_alpha);
/* Quantized low/high code bytes, without expansion through an eight-bit
 * colour value. Finite binary32 to UNORM1..16, clamped to [0,1] on GS. */
int ps2_native_float_unorm_words(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t [4],const uint32_t [2],const unsigned [3],uint32_t,unsigned,unsigned);
/* Low byte of the R5900 unsigned conversion: truncate nonnegative finite
 * binary32, clamp negatives to zero and >=2^32 to UINT32_MAX. GS only. */
int ps2_native_float_byte(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t value[4],uint32_t output,uint32_t palette,unsigned width,unsigned height);
/* Pre-filtered nonnegative binary32 RGB and replicated-alpha planes. Input
 * planes are read-only and may share storage; outputs are distinct expanded
 * UNORM1..8 code planes. Scalar masks share their RGB/alpha plane handles.
 * The sampled values and every blend operation remain on GS. */
int ps2_native_saturate_float(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[4],const uint32_t source_alpha[4],
    const uint32_t mask[4],const uint32_t mask_alpha[4],uint32_t destination,uint32_t destination_alpha,
    const struct ps2_unorm_layout *destination_layout,uint32_t palette,unsigned width,unsigned height);
/* Alpha-only Saturate, for nonnegative sampled binary32 alpha in [0,1].
 * The UNORM1..8 result is clamp(Sa*Ma+Da): the saturated branch quantizes
 * to the maximum code even with binary32 rounding. Avoid computing unused
 * RGB factors and division. Inputs are read-only; destination is expanded. */
int ps2_native_saturate_alpha_float(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source_alpha[4],const uint32_t mask_alpha[4],uint32_t destination_alpha,
    unsigned alpha_bits,uint32_t palette,unsigned width,unsigned height);
/* Optional original destination-map alpha precision and separately quantized
 * alpha/RGB results. Parent and map encode the same GS float result directly,
 * retaining different UNORM widths without conversion through parent bytes. */
struct ps2_saturate_map_target {
    unsigned input_bits;
    uint32_t map;
    unsigned map_bits;
    uint32_t colour;
    unsigned rgb_bits[3];
};
int ps2_native_saturate_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[4],const uint32_t source_alpha[4],
    const uint32_t mask[4],const uint32_t mask_alpha[4],uint32_t destination,uint32_t destination_alpha,
    const struct ps2_unorm_layout *,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height);
int ps2_native_saturate_alpha_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source_alpha[4],const uint32_t mask_alpha[4],uint32_t destination_alpha,
    unsigned alpha_bits,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height);
/* Remaining disjoint/conjoint operators use independently rounded binary32
 * factors, products and sum on GS. The operation is a ps2_picture_operation
 * from DISJOINT_OVER through CONJOINT_XOR; all other values reject before
 * allocation or drawing. Inputs and map publication follow Saturate above. */
int ps2_native_porter_duff_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[4],const uint32_t source_alpha[4],
    const uint32_t mask[4],const uint32_t mask_alpha[4],uint32_t destination,uint32_t destination_alpha,
    const struct ps2_unorm_layout *,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height,unsigned operation);
int ps2_native_porter_duff_alpha_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source_alpha[4],const uint32_t mask_alpha[4],uint32_t destination_alpha,
    unsigned alpha_bits,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height,unsigned operation);
/* Exact component-wise 24x24 -> 48-bit product in six little-endian byte
 * planes. Each RGB lane is independent. Origin pages are 64x32 CT32.
 * Outputs may alias inputs; all input reads finish before publication.
 * This register arithmetic does not advertise additional Render operators. */
int ps2_native_product24(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t a[3],const uint32_t b[3],const uint32_t product[6],
    uint32_t palette,unsigned width,unsigned height);
/* Convert bit-expanded RGB to the wide renderer's IEEE binary32 byte planes.
 * Component widths are 1..8. Constant reciprocals use pixman's normalization
 * convention; source-dependent table selection and all output writes are GS.
 * Input and four output pages must be distinct. */
int ps2_native_unorm_float(struct ps2_surface_client *,uint32_t expanded_rgb,
    const uint32_t floating[4],const unsigned component_bits[3],uint32_t palette,
    unsigned width,unsigned height);
/* Binary32 multiplication for nonnegative finite normal operands or zero,
 * with finite normal products or zero. UNORM-derived operands and the
 * larger magnitudes in signed SoftLight intermediates satisfy this domain.
 * Round-to-nearest/even runs on GS; input/output byte-plane aliases are valid. */
int ps2_native_float_multiply(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t a[4],const uint32_t b[4],const uint32_t product[4],
    uint32_t palette,unsigned width,unsigned height);
/* Binary32 division: numerator is zero or positive normal; denominator is
 * positive normal. The quotient must be zero or positive normal and finite.
 * Round-to-nearest/even, including the remainder, runs on GS. No subnormal,
 * signed, infinity or NaN contract. Outputs may alias either input. */
int ps2_native_float_divide(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t numerator[4],const uint32_t denominator[4],const uint32_t quotient[4],
    uint32_t palette,unsigned width,unsigned height);
/* Nonnegative binary32 addition, or subtraction with a>=b in every lane.
 * Operands and result are zero or positive normal and finite. Addition may
 * accept either operand order. All alignment, sticky bits, cancellation and
 * round-to-nearest/even run on GS. Outputs may alias either input. */
int ps2_native_float_add(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t a[4],const uint32_t b[4],const uint32_t result[4],uint32_t palette,
    unsigned width,unsigned height,unsigned subtract);
enum ps2_float_relation { PS2_FLOAT_LT,PS2_FLOAT_LE,PS2_FLOAT_GT,PS2_FLOAT_GE };
/* Positive-normal or +zero binary32 comparison, one Boolean RGB byte per lane.
 * Selection copies complete binary32 registers on GS. Outputs may alias
 * inputs (including cross-byte aliases); reads finish before publication. */
int ps2_native_float_compare(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t a[4],const uint32_t b[4],uint32_t predicate,uint32_t palette,
    unsigned width,unsigned height,unsigned relation);
int ps2_native_float_select(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t yes[4],const uint32_t no[4],uint32_t predicate,const uint32_t result[4],
    uint32_t palette,unsigned width,unsigned height);
/* Correctly rounded square root of positive-normal or +zero binary32.
 * A 24-step restoring integer square root and its remainder run on GS.
 * Input/output aliases, including reversed byte order, are supported. */
int ps2_native_float_sqrt(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t value[4],const uint32_t result[4],uint32_t palette,unsigned width,unsigned height);
enum ps2_float_arithmetic { PS2_FLOAT_ADD,PS2_FLOAT_SUBTRACT,PS2_FLOAT_MULTIPLY,PS2_FLOAT_DIVIDE };
/* Signed finite normal arithmetic, or zero; results must remain finite and
 * normal (or zero). Division requires a nonzero denominator. Each operation
 * rounds separately to nearest/even, and zero results are canonical +zero.
 * Signs, magnitude ordering and arithmetic stay on GS; aliases are valid. */
int ps2_native_float_signed(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t a[4],const uint32_t b[4],const uint32_t result[4],uint32_t palette,
    unsigned width,unsigned height,unsigned operation);
/* HSL's product/division pairs additionally preserve signed subnormal
 * products. Multiply takes normal/zero inputs; divide accepts a subnormal
 * numerator and a normal nonzero denominator, with a normal/zero quotient.
 * Add/subtract retain the normal/zero domain above. */
int ps2_native_float_signed_tiny(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t a[4],const uint32_t b[4],const uint32_t result[4],uint32_t palette,
    unsigned width,unsigned height,unsigned operation);
/* Independent RGB channel routing, and numeric comparison of signed normal
 * binary32 or signed zero. Routing selects source channels 0..2; outputs may
 * alias inputs, including cross-byte aliases. All values stay on GS. */
int ps2_native_float_swizzle(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t value[4],const unsigned channels[3],const uint32_t result[4],
    uint32_t palette,unsigned width,unsigned height);
int ps2_native_float_signed_compare(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t a[4],const uint32_t b[4],uint32_t predicate,uint32_t palette,
    unsigned width,unsigned height,unsigned relation);
/* Wide ColorDodge/ColorBurn. The mode is 0/1, respectively. Original sampled
 * source and mask registers remain read-only; zero branches, comparisons,
 * separately rounded arithmetic and independent parent/map stores use GS. */
int ps2_native_dodge_burn_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[4],const uint32_t source_alpha[4],
    const uint32_t mask[4],const uint32_t mask_alpha[4],uint32_t destination,uint32_t destination_alpha,
    const struct ps2_unorm_layout *,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height,unsigned mode);
int ps2_native_dodge_burn_alpha_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source_alpha[4],const uint32_t mask_alpha[4],uint32_t destination_alpha,
    unsigned alpha_bits,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height,unsigned mode);
int ps2_native_softlight_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[4],const uint32_t source_alpha[4],
    const uint32_t mask[4],const uint32_t mask_alpha[4],uint32_t destination,uint32_t destination_alpha,
    const struct ps2_unorm_layout *,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height);
int ps2_native_softlight_alpha_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source_alpha[4],const uint32_t mask_alpha[4],uint32_t destination_alpha,
    unsigned alpha_bits,const struct ps2_saturate_map_target *,uint32_t palette,unsigned width,unsigned height);
/* HSL modes 0..3: Hue, Saturation, Color, Luminosity. The mask is scalar;
 * mask and mask_alpha must name the same four planes. Source/mask registers
 * remain read-only; all channel ordering and clipping are GS operations. */
int ps2_native_hsl_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[4],const uint32_t source_alpha[4],
    const uint32_t mask[4],const uint32_t mask_alpha[4],uint32_t destination,uint32_t destination_alpha,
    const struct ps2_unorm_layout *,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height,unsigned mode);
/* Same result, but source is writable scratch after its masked copy. Its
 * planes must not alias source_alpha or mask. Saves four live GS surfaces. */
int ps2_native_hsl_float_mapped_consume(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[4],const uint32_t source_alpha[4],
    const uint32_t mask[4],const uint32_t mask_alpha[4],uint32_t destination,uint32_t destination_alpha,
    const struct ps2_unorm_layout *,const struct ps2_saturate_map_target *,
    uint32_t palette,unsigned width,unsigned height,unsigned mode);
int ps2_native_hsl_alpha_float_mapped(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source_alpha[4],const uint32_t mask_alpha[4],uint32_t destination_alpha,
    unsigned alpha_bits,const struct ps2_saturate_map_target *,uint32_t palette,unsigned width,unsigned height,unsigned mode);
/* Clamp nonnegative finite binary32 to [0,1] and quantize each RGB lane to
 * 1..8 UNORM bits using Render's floor(f*2^bits), with 1 mapping to the
 * maximum code. Codes occupy the low bits of each destination RGB byte.
 * Input/output aliases are valid; destination alpha is preserved. */
int ps2_native_float_unorm(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t floating[4],uint32_t codes,const unsigned component_bits[3],
    uint32_t palette,unsigned width,unsigned height);
/* Pinned R5900 wide-convolution accumulation. Four byte planes hold
 * float((uint32_t)accumulator) * 2^-32, initially zero. Each tap performs
 * binary32 UNORM*coefficient, addition, unsigned truncation/clamping and the
 * next uint32-to-float rounding on GS. Constant product tables depend only
 * on the coefficient and original channel widths, never sampled pixels.
 * End applies pixman's signed-int32 / 65536 and [0,1] clamp. */
int ps2_native_convolution_float_tap(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    uint32_t expanded_rgb,const unsigned component_bits[3],int32_t coefficient,
    const uint32_t accumulator[4],uint32_t palette,unsigned width,unsigned height);
/* The same R5900 tap semantics for original-precision, nonnegative binary32
 * samples, including ten-bit RGB and two-bit alpha. Sample planes are
 * read-only and distinct from the accumulator; no pixel leaves the GS. */
int ps2_native_convolution_float_sample(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t sample[4],int32_t coefficient,const uint32_t accumulator[4],
    uint32_t palette,unsigned width,unsigned height);
int ps2_native_convolution_float_end(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t accumulator[4],const uint32_t floating[4],uint32_t palette,
    unsigned width,unsigned height);
/* Wide nearest Saturate on canonical UNORM8 RGB and replicated alpha pages.
 * Masks are RGB plus independent replicated alpha (the same page for scalar
 * coverage). Destination RGB and alpha update in place. The other pages must
 * be distinct. Every blend, zero-alpha branch and quantization runs on GS. */
int ps2_native_saturate_rgba8(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    uint32_t source_rgb,uint32_t source_alpha,uint32_t mask_rgb,uint32_t mask_alpha,
    uint32_t destination_rgb,uint32_t destination_alpha,uint32_t palette,
    unsigned width,unsigned height);
/* Nearest Saturate for 1..8-bit UNORM channels in bit-expanded CT32 pages.
 * Layouts are source, mask and destination. Results are expanded back into
 * the same representation for existing GS packed-format encoders. */
int ps2_native_saturate_unorm(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    uint32_t source_rgb,uint32_t source_alpha,uint32_t mask_rgb,uint32_t mask_alpha,
    uint32_t destination_rgb,uint32_t destination_alpha,const struct ps2_unorm_layout layouts[3],
    uint32_t palette,unsigned width,unsigned height);
#endif
