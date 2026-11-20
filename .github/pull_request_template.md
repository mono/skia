<!--
Thanks for contributing to the SkiaSharp fork of Skia!

• Target the `skiasharp` branch (our long-lived integration branch) — not `main`.
• This fork adds the C API shim in include/c/ and src/c/ plus build tweaks.
  Keep changes minimal and additive so upstream Skia merges stay clean.
• Just fill in the Description — the other sections have sensible defaults. Each
  section's comment holds a copy-paste block; drop it in only when you have something.
-->

## Description

<!--
What does this change do, and why?

• New or changed C API? Say what SkiaSharp scenario it unblocks, and list the
  exact exports under Changes below.
• Upstream merge / milestone bump? Note the milestone number and the upstream
  Skia ref (tag or commit) you merged/rebased onto, plus any C API fix-ups needed.
• Dependency bump (DEPS)? Note the dependency, old → new version, and any CVE(s)
  it addresses, with a link to the upstream changelog.
-->

**SkiaSharp issue**

Related SkiaSharp issue: <issue-url>

<!-- Ensure a SkiaSharp issue exists for the feature or bug before opening this PR. -->

**Required SkiaSharp PR**

Requires SkiaSharp PR: <pull-request-url>

<!--
Every change here — including build, CI, or infrastructure — needs a companion
SkiaSharp PR to actually take effect: at minimum one that bumps the SkiaSharp
submodule to this commit. A C API change additionally needs that PR to regenerate
the P/Invoke bindings (pwsh ./utils/generate.ps1) and add the managed wrapper +
tests. Link it above; open the SkiaSharp PR first if it doesn't exist yet.
-->

**Areas affected**

- [ ] C API (`include/c`, `src/c`)
- [ ] Native dependency / `DEPS`
- [ ] Build (gn / build files)
- [ ] Upstream Skia merge or rebase
- [ ] Rendering output / behavior
- [ ] Other

## Changes

None.

<!--
Scope: the exported C API surface and any observable BEHAVIOR — NOT a file-by-file
list. The diff already shows which files changed; the "what & why" belongs in
Description above. An upstream-merge or build-only PR that changes neither can
just leave "None."

The C API is consumed by SkiaSharp's generated P/Invoke bindings, so any change
here must be paired with a SkiaSharp PR that regenerates them. Copy the parts that
apply over "None." and delete the rest:

**C API**

```c
// added
void sk_canvas_draw_foo(sk_canvas_t* canvas, float x, float y, const sk_paint_t* paint);

// renamed / changed
void sk_object_old_name()  =>  void sk_object_new_name()
```

**Behavior**

A change to rendering output, defaults, or behavior an upgrading app would notice.
-->

## Testing

<!--
How did you verify this builds and behaves correctly? Note which platforms/backends
you built/ran on (CPU, GPU/Metal/Vulkan/ANGLE, Windows/macOS/Linux/Android/iOS/WASM)
and anything you could NOT test. Most functional verification happens in the
companion SkiaSharp PR — link its tests.

Rendering change? Copy this in to show the difference:

<details>
<summary>Screenshots (before / after)</summary>

| Before | After |
| --- | --- |
|  |  |

</details>
-->

## Checklist

- [ ] Targets the `skiasharp` branch
- [ ] `Changes` above lists every added/changed C API export (or "None.")
- [ ] Companion SkiaSharp PR linked above (submodule bump at minimum; regenerates bindings + adds tests for C API changes)
