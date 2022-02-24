# UAM - deko3d shader compiler

```
Usage: uam [options] file
Options:
  -o, --out=<file>   Specifies the output deko3d shader module file (.dksh)
  -r, --raw=<file>   Specifies the file to which output raw Maxwell bytecode
  -t, --tgsi=<file>  Specifies the file to which output intermediary TGSI code
  -s, --stage=<name> Specifies the pipeline stage of the shader
                     (vert, tess_ctrl, tess_eval, geom, frag, comp)
  -v, --version      Displays version information
```

UAM is the shader compiler designed to produce precompiled DKSH shaders usable with the deko3d graphics API, specifically for the Nvidia Tegra X1 processor found inside the Nintendo Switch.

UAM is based on [mesa](https://www.mesa3d.org/)'s GLSL parser and TGSI infrastructure; as well as nouveau's nv50_ir code generation backend. As such, it inherits all the capabilities and the feature set (GLSL extension support) offered by mesa/nouveau for GM20x GPUs. In addition, there are a number of customizations and codegen improvements that produce code better suited for use with deko3d.

## Differences with standard GL and mesa/nouveau

- The `DEKO3D` preprocessor symbol is defined, with a value of 100.
- UBO, SSBO, sampler and image bindings are **required to be explicit** (i.e. `layout (binding = N)`), and they have a one-to-one correspondence with deko3d bindings. Failure to specify explicit bindings will result in an error.
- There is support for 16 UBOs, 16 SSBOs, 32 "samplers" (combined image+sampler handle), and 8 images for each and every shader stage; with binding IDs ranging from zero to the corresponding limit minus one. However note that due to hardware limitations, only compute stage UBO bindings 0-5 are natively supported, while 6-15 are emulated as "SSBOs".
- Default uniforms outside UBO blocks (which end up in the internal driver const buffer) are detected, however they are reported as an error due to lack of support in both DKSH and deko3d for retrieving the location of and setting these uniforms.
- Internal deko3d constbuf layout and numbering schemes are used, as opposed to nouveau's.
- `gl_FragCoord` always uses the Y axis convention specified in the flags during the creation of a deko3d device. `layout (origin_upper_left)` has no effect whatsoever and produces a warning, while `layout (pixel_center_integer)` is not supported at all and produces an error.
- Integer divisions and modulo operations with non-constant divisors decay to floating point division, and generate a warning. Well written shaders should avoid these operations for performance and accuracy reasons. (Also note that unmodified nouveau, in order to comply with the GL standard, emulates integer division/module with a software routine that has been removed in UAM)
- 64-bit floating point divisions and square roots can only be approximated with native hardware instructions. This results in loss of accuracy, and as such these operations should be avoided, and they generate a warning as well. (Also note that likewise, unmodified nouveau uses a software routine that has been removed in UAM)
- Transform feedback is not supported.
- GLSL shader subroutines (`ARB_shader_subroutine`) are not supported.
- There is no concept of shader linking. Separable programs (`ARB_separate_shader_objects`) are always in effect.
- The compiler is based on mesa 19.0.8 sources; however several cherrypicked bugfixes from mesa 19.1 and up have been applied.
- Numerous codegen differences:
	- Added **Maxwell dual issue** scheduling support based on the groundwork laid out by karolherbst's [dual_issue_v3](https://github.com/karolherbst/mesa/commits/dual_issue_v3) branch, and enhanced with new experimental findings.
	- Removed bound checks in SSBO accesses.
	- Removed bound checks in atomic accesses.
	- Removed bound checks in image accesses.
	- Multisampled texture lookups use optimized bitwise logic with hardcoded sample positions instead of requiring helper data in the driver constbuf.
	- Multisampled image operations use `TXQ` instead of requiring helper data in the driver constbuf.
	- Non-bindless image operations are supported natively instead of being emulated with bindless operations.
	- SSBO size calculations use unsigned math instead of signed math, which results in better codegen.
	- `ballotARB()` called with a constant argument now results in optimal codegen using the PT predicate register.
	- **Bugfixes**:
		- Bindless texture queries were broken.
		- `IMAD` instruction encoding with negated operands was broken.
	- Minor changes done to match properties observed in official shader code:
		- `MOV Rd,RZ` is now preferred to `MOV32I Rd,0`.
		- `LDG`/`STG` instructions are used for SSBO accesses instead of `LD`/`ST`.
		- Shader programs are properly padded out to a size that is a multiple of 64 bytes.
