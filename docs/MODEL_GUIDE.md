# Choosing, installing and managing AI models

This guide explains the model-management tasks that Ask the Model (AtM) does not yet perform inside its own interface. It is written primarily for the recommended reference setup: AtM on Linux with a local Ollama provider.

AtM v0.2.1 discovers models through the provider API. It does not crawl the filesystem for model files and it does not currently download, import, move or delete models.

## 1. Understand the three separate components

The local AI stack has three distinct layers:

1. **Ask the Model** — the chat interface.
2. **The provider** — software such as Ollama that loads models, runs inference and exposes an API.
3. **The model** — the downloaded weights and metadata used for inference.

Installing AtM does not install a model. Installing Ollama does not automatically mean that a useful chat model is already present. A model must be downloaded or imported into the provider before AtM can discover it.

## 2. The simplest model source: Ollama Library

For a first setup, start with the [Ollama Library](https://ollama.com/library). It exposes models and variants in a form that Ollama can download directly.

Typical workflow:

```bash
ollama pull qwen3.5:2b-q4_K_M
ollama ls
```

`ollama pull` downloads or updates the model. `ollama ls` confirms that the provider has registered it.

Once the model is visible in `ollama ls`, start AtM or use **Refresh models**. AtM will ask the provider for its installed models and will add compatible chat/completion models to the model selector automatically.

Official reference: [Ollama CLI](https://docs.ollama.com/cli).

## 3. A larger model source: Hugging Face

[Hugging Face Models](https://huggingface.co/models) contains a much larger catalog. For local inference, [GGUF-filtered models](https://huggingface.co/models?library=gguf) are especially relevant because GGUF is widely used by local inference engines, including Ollama and llama.cpp.

Do not download a model based only on its name or popularity. Read the model card first. Hugging Face describes model cards as the place where authors can document intended uses, limitations, evaluation results, datasets, languages and licensing.

Useful references:

- [Hugging Face: GGUF](https://huggingface.co/docs/hub/gguf)
- [Hugging Face: Model Cards](https://huggingface.co/docs/hub/model-cards)
- [Hugging Face: repository licenses](https://huggingface.co/docs/hub/repositories-licenses)

## 4. What GGUF means for AtM users

GGUF is a model file format designed for efficient local inference. A GGUF file is not automatically an AtM model merely because it exists on your disk.

AtM discovers models from the provider. Therefore an external GGUF must first be imported into a provider that AtM can query.

For Ollama, the official import procedure for a single-file GGUF is:

1. Create a file named `Modelfile`.
2. Point `FROM` to the GGUF file.
3. Run `ollama create`.
4. Confirm the resulting model with `ollama ls`.

Example:

```text
FROM /path/to/file.gguf
```

Then:

```bash
ollama create my-model
ollama ls
```

After the provider registers `my-model`, use **Refresh models** in AtM.

Ollama does not quantize a GGUF during this import step. The GGUF should already be in the representation you intend to use. See [Ollama: Importing a Model](https://docs.ollama.com/import).

## 5. How to read model names and variants

Model names often encode several pieces of information.

### Parameter scale

Labels such as `0.8B`, `2B`, `4B`, `9B` or `27B` refer approximately to the number of model parameters. Parameter count is useful for estimating the general scale of a model, but it is not a complete measure of quality, speed or memory usage.

As model scale increases, storage and runtime memory requirements usually increase as well.

### Quantization

Quantization stores model weights at lower numerical precision to reduce storage and memory use. Lower precision can also improve inference efficiency on compatible hardware, but the trade-off is reduced numerical fidelity.

Common GGUF labels include:

- `Q4_K_M` — a commonly encountered 4-bit-class K-quantized variant;
- `Q8_0` — higher precision and generally larger than a Q4 variant;
- `F16` / `BF16` — much larger high-precision variants.

Do not interpret a quantization label as a universal quality score. The effect varies by model, task, context length and hardware.

General reference: [Hugging Face: Quantization concepts](https://huggingface.co/docs/transformers/quantization/concept_guide).

### Context window

The context window describes how much text/history a model can process under a given configuration. A model page may advertise a very large maximum context, but using larger context can increase runtime memory requirements. Provider defaults may also be lower than the model maximum.

For Ollama configuration details, see the [Ollama FAQ](https://docs.ollama.com/faq).

## 6. Starter examples

The following examples were checked against the Ollama Library on 2026-09-19. They are not a benchmark ranking and do not imply that one is universally better than another.

| Model / variant | Approx. listed download | Example command |
| --- | ---: | --- |
| Qwen 3.5 0.8B | 1.0 GB | `ollama pull qwen3.5:0.8b` |
| Qwen 3.5 2B Q4_K_M | 1.9 GB | `ollama pull qwen3.5:2b-q4_K_M` |
| Phi-4 Mini 3.8B Q4_K_M | 2.5 GB | `ollama pull phi4-mini:3.8b-q4_K_M` |
| Qwen 3.5 4B Q4_K_M | 3.4 GB | `ollama pull qwen3.5:4b-q4_K_M` |

Sources:

- [Qwen 3.5 tags on Ollama](https://ollama.com/library/qwen3.5/tags)
- [Phi-4 Mini tags on Ollama](https://ollama.com/library/phi4-mini/tags)

The listed download size is not the same as total RAM or VRAM required at runtime. Runtime memory also depends on context, cache allocation, provider settings and how much of the model is placed on CPU versus GPU.

## 7. A practical model-selection process

For a new machine or provider configuration, use a conservative progression rather than downloading many large models at once.

1. Start with a small model that clearly fits your disk and memory budget.
2. Confirm the complete workflow: provider → model → AtM discovery → chat response.
3. Run `ollama ps` while the model is loaded and inspect the `PROCESSOR` column.
4. If performance is acceptable, try a larger model only if you have a reason to do so.
5. Compare models using the tasks that matter to you rather than relying only on public rankings.
6. Remove models that are not useful so they do not consume storage indefinitely.

## 8. Check whether the model is using CPU or GPU

Run:

```bash
ollama ps
```

Ollama documents the `PROCESSOR` column as indicating where a model is loaded. Examples include:

- `100% GPU` — entirely in GPU memory;
- `100% CPU` — entirely in system memory;
- a mixed CPU/GPU percentage — split between both.

A model that relies heavily on CPU/system memory can still work, but may respond much more slowly than a model that fits efficiently on the available GPU.

## 9. Stop a model versus delete a model

These are different operations.

### Free RAM/VRAM but keep the model

```bash
ollama stop MODEL
```

This unloads the model from memory but does not remove the installed model from disk.

Ollama normally keeps models loaded for a period after use to speed up subsequent requests; its current FAQ documents a default of approximately five minutes.

### Remove the installed model and free disk space

```bash
ollama rm MODEL
```

After removal, press **Refresh models** in AtM. The removed model should no longer appear in the selector.

## 10. List installed and loaded models

Installed models:

```bash
ollama ls
```

Currently loaded models:

```bash
ollama ps
```

This distinction is useful when diagnosing both disk-space and memory problems.

## 11. Where Ollama stores model data

Ollama currently documents these default model locations:

| Operating system | Default model store |
| --- | --- |
| Linux | `/usr/share/ollama/.ollama/models` |
| macOS | `~/.ollama/models` |
| Windows | `C:\Users\%username%\.ollama\models` |

AtM v0.2.1 is packaged primarily for Linux/elementary OS, but the other paths are included here because they are part of the provider documentation.

Do not normally delete individual files or blobs directly from the provider model store. Use `ollama rm MODEL` so the provider remains aware of the change.

## 12. Move the model store to another drive

Large model collections can consume substantial SSD space. Ollama allows the model directory to be changed with the `OLLAMA_MODELS` environment variable.

On Linux with the standard Ollama system service, the `ollama` user must have read/write access to the selected directory.

The Ollama FAQ gives the following ownership pattern:

```bash
sudo chown -R ollama:ollama <directory>
```

Configure the environment variable using the method appropriate to how the Ollama service is started, then restart the provider.

Before deleting the old model store, confirm that `ollama ls` sees the expected models from the new location.

Official reference: [Ollama FAQ — model storage](https://docs.ollama.com/faq).

## 13. Model licenses are separate from the AtM license

AtM is distributed under the MIT License. That license applies to AtM, not to every model that can be used with it.

Models may use MIT, Apache-2.0, OpenRAIL-family terms, Llama community licenses, Gemma terms, research licenses, custom licenses or other conditions.

Before using or redistributing a model:

1. read its model card;
2. identify the license;
3. check usage restrictions;
4. check whether redistribution or commercial use is permitted for your intended case.

Hugging Face model pages can expose license metadata, but you should still read the model documentation and license text where relevant.

## 14. Local model versus cloud-backed model

AtM connects only to loopback provider addresses in v0.2.1. This means AtM talks to software on the same machine. It does not guarantee that every model exposed by that software performs inference locally.

For example, current Ollama versions can expose cloud functionality in addition to local models.

If strict local-only behavior matters:

1. choose a model that is actually installed locally;
2. verify local provider behavior;
3. consider disabling provider cloud features;
4. review provider privacy documentation.

Ollama currently documents local-only mode through either:

```text
OLLAMA_NO_CLOUD=1
```

or `disable_ollama_cloud` in its server configuration. See the [Ollama FAQ](https://docs.ollama.com/faq).

## 15. When AtM does not show a model

Work through these checks in order:

1. Run `ollama ls`. If the model is absent, the provider has not registered it.
2. If you downloaded a raw GGUF, import it into the provider rather than leaving it as an arbitrary file.
3. Make sure the provider is reachable on an endpoint AtM currently supports.
4. Press **Refresh models** in AtM.
5. Remember that AtM intentionally filters out models that do not advertise the `completion` capability.

For detailed diagnostics, see [Troubleshooting](TROUBLESHOOTING.md).

## Official references

- [Ollama CLI reference](https://docs.ollama.com/cli)
- [Ollama FAQ](https://docs.ollama.com/faq)
- [Ollama model import](https://docs.ollama.com/import)
- [Ollama Library](https://ollama.com/library)
- [Hugging Face GGUF documentation](https://huggingface.co/docs/hub/gguf)
- [Hugging Face Model Cards](https://huggingface.co/docs/hub/model-cards)
- [Hugging Face licenses](https://huggingface.co/docs/hub/repositories-licenses)
- [Hugging Face quantization concepts](https://huggingface.co/docs/transformers/quantization/concept_guide)
