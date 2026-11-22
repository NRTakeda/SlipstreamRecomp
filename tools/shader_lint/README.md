# Shader translation lint

Checks over translated GLSL for the bug classes that have shown up as wrong pictures. All read files, none needs
the game running except where said.

Get every translation the game uses (the precompile list of a data root that has played the game) with the
instruction comments:

```
SAVAGEWIND_DATA_ROOT=<fresh data root with cache/vk_shader_keys.list copied in> SAVAGEWIND_SAVE_MSL=1 \
SAVAGEWIND_XLATE_DUMP=<empty dir> SAVAGEWIND_PRECOMPILE_NO_PIPELINES=1 build/savagewind --precompile-shaders <game>
```

(a data root whose cache already holds the translations makes none: start from an empty cache/xlate).

- `same_insn_reads.py <dir>`: a statement reading a register another statement of the same guest instruction already
  wrote (the instruction reads its sources first). The translator guards these itself (translate.c
  guard_self_reads, logged as `[xlate] ... guarded`); this finds them in dumps, per opcode.
- `unwritten_reads.py <dir>`: registers or predicates read but never written (a dropped instruction).
- `equal_depth_pairs.py <debug log> <glsl dir>`: each depth-EQUAL draw of a debug frame (`SAVAGEWIND_DEBUG_FRAME`,
  with `SAVAGEWIND_SAVE_MSL=1 SAVAGEWIND_NO_XLATE_CACHE=1` for the sources in build/glsl) against the depth-writing
  draw before it with the same vertex data: whether both vertex shaders compute gl_Position as the same expression
  (commutative operands normalised). A difference means the colour pass can fail its EQUAL test (grey or missing
  objects). Needs the debug log's `vbuf` per attribute.
