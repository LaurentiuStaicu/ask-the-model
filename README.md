<p align="center">
  <img src="assets/icon.png" alt="Ask the Model icon" width="112">
</p>

<h2 align="center">Ask the Model (AtM)</h2>

<p align="center">
  <a href="https://github.com/LaurentiuStaicu/ask-the-model/releases/latest"><img alt="Version: 0.2.2" src="https://img.shields.io/github/v/tag/LaurentiuStaicu/ask-the-model?sort=semver&style=flat-square&label=release&color=333333"></a>
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-707070?style=flat-square"></a>
  <a href="#supported-platform-and-compatibility"><img alt="Linux / Flatpak" src="https://img.shields.io/badge/platform-Linux%20%2F%20Flatpak-a0a0a0?style=flat-square"></a>
</p>

<p align="center">
  <a href="https://github.com/LaurentiuStaicu/ask-the-model/releases/latest/download/AskTheModel.flatpak"><img alt="Download Flatpak" src="https://img.shields.io/badge/download-Flatpak-333333?style=flat-square&logo=flatpak&logoColor=white"></a>
  <a href="#start-here-first-time-setup"><img alt="Getting started" src="https://img.shields.io/badge/read-Getting%20started-707070?style=flat-square"></a>
  <a href="docs/MODEL_GUIDE.md"><img alt="Model guide" src="https://img.shields.io/badge/read-Model%20guide-a0a0a0?style=flat-square"></a>
</p>

<p align="center"><small><strong>A local-first desktop interface for chatting with locally managed AI models and, progressively, for querying and exploring repositories of scientific dynamical models.</strong></small></p>

<p align="center"><small><a href="#start-here-first-time-setup">First-time setup</a> · <a href="#finding-and-choosing-an-ai-model">Choosing a model</a> · <a href="#managing-models-disk-space-and-memory">Managing models</a> · <a href="docs/TROUBLESHOOTING.md">Troubleshooting</a> · <a href="STATUS.md">Project status</a></small></p>

---

### What is Ask the Model?

<small>Ask the Model (AtM) is the desktop interface you use to write prompts, choose an available AI model and read streamed responses. It is deliberately separate from the software that actually runs the model and from the model files themselves.</small>

<p align="center"><code>You → Ask the Model → local provider → AI model</code></p>

<small><strong>Ask the Model</strong> provides the chat interface. A <strong>local provider</strong>, such as Ollama, loads and runs AI models and exposes a local API. The <strong>AI model</strong> is a separate set of files, often several gigabytes in size, that must currently be downloaded and managed outside AtM.</small>

<small>This distinction matters in the current release: AtM can automatically discover compatible models that your provider already knows about, but AtM v0.2.2 does <strong>not</strong> download, import, move or delete AI models for you.</small>

<small>The longer-term purpose of AtM is broader than ordinary local chat. It is intended to become a natural-language access layer for scientific dynamical-model repositories such as Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD), without replacing those repositories as the authoritative source for their code, data, assumptions, provenance or validation.</small>

### Before you begin

<small>You need four things for a normal first run:</small>

- <small><strong>Linux with Flatpak</strong> — used to install and run AtM.</small>
- <small><strong>A local Ollama-compatible provider</strong> — the software that actually loads and runs AI models.</small>
- <small><strong>At least one chat-capable AI model</strong> — downloaded separately through the provider; AtM does not currently download it for you.</small>
- <small><strong>Enough free disk space and runtime memory</strong> — model downloads commonly occupy gigabytes, and running a model also requires RAM and, where available, GPU memory.</small>

<small>You also need Internet access for the initial AtM/provider/model downloads. Once a genuinely local model is installed, the current AtM chat path itself talks to the provider through the local loopback interface.</small>

<small>If any of these terms are unfamiliar, continue in order rather than skipping ahead. Each step below includes the result you should expect before moving to the next one.</small>

### Start here: first-time setup

<small>The steps below are written for someone installing AtM for the first time. The recommended reference setup uses <a href="https://ollama.com/">Ollama</a> as the local provider because its model library and command-line tools make model installation and management comparatively straightforward. Other providers may work only if they expose the Ollama-compatible API behavior documented in <a href="docs/DEPENDENCIES_AND_COMPATIBILITY.md">Dependencies and compatibility</a>.</small>

#### 1. Check that Flatpak is available

```bash
flatpak --version
```

<small>If this command is not available, install Flatpak using the instructions for your Linux distribution before installing AtM. AtM is distributed as a Flatpak bundle; normal users do not need to install Meson, Vala, GTK development packages or the elementary SDK.</small>

<small><strong>Expected result:</strong> the command prints a Flatpak version number and returns without an error.</small>

#### 2. Install and start a local AI provider

<small>For a new setup, install Ollama from the <a href="https://ollama.com/download/linux">official Linux download page</a>. AtM first checks the standard Ollama endpoint at <code>127.0.0.1:11434</code> and then a compatibility fallback at <code>127.0.0.1:11435</code>.</small>

<small>A simple provider check is:</small>

```bash
ollama ls
```

<small>If Ollama is running, this command returns the models currently installed in its local model store. An empty list is valid; it simply means that you still need to download a model.</small>

<small><strong>Expected result:</strong> <code>ollama ls</code> responds normally. It may show zero models at this stage; a connection error means the provider is not ready yet.</small>

#### 3. Choose and download your first AI model

<small>AtM does not contain an AI model and does not currently download one automatically. You must install at least one chat-capable model through the provider before AtM can chat.</small>

<small>For a small first test, one current example is Qwen 3.5 2B in the Q4_K_M variant:</small>

```bash
ollama pull qwen3.5:2b-q4_K_M
```

<small>The Ollama library currently lists this variant at about 1.9 GB. This is an example, not a claim that it is the best model for every computer or task. See <a href="#finding-and-choosing-an-ai-model">Finding and choosing an AI model</a> below before installing larger models.</small>

<small>After the download completes, verify that the provider has registered it:</small>

```bash
ollama ls
```

<small><strong>Expected result:</strong> the model name appears in the list. This is the point at which AtM can discover it through the provider.</small>

#### 4. Download and install Ask the Model

<small>Download the latest release bundle from <a href="https://github.com/LaurentiuStaicu/ask-the-model/releases/latest/download/AskTheModel.flatpak">AskTheModel.flatpak</a>, then install it for your user account:</small>

```bash
flatpak install --user ./AskTheModel.flatpak
```

<small>Flatpak resolves the application runtime separately. You do not need to install GTK, Granite, libsoup or json-glib development packages manually just to use the release bundle.</small>

<small><strong>Expected result:</strong> Flatpak finishes installation without an error and registers <code>io.github.laurentiustaicu.ask_the_model</code> for your user account.</small>

#### 5. Start AtM

```bash
flatpak run io.github.laurentiustaicu.ask_the_model
```

<small>When AtM starts, it automatically probes the supported local provider endpoints, asks the provider which models are installed, checks which of those models are chat/completion-capable, and fills the model selector with the compatible models it finds. If at least one compatible model is available, AtM selects one automatically.</small>

<small><strong>Expected result:</strong> the title bar shows a compatible AI model instead of <code>No chat models detected</code>, and you can send a prompt. A brief scan status such as <code>1 model found</code> may appear while discovery finishes.</small>

#### 6. Download more models later and use Refresh

<small>You can install another model at any time with your provider. If AtM is already open when the download finishes, click the circular <strong>Refresh models</strong> button in the title bar. AtM repeats model discovery and updates the selector; you do not need to add the model to AtM manually.</small>

<small><strong>Expected result:</strong> compatible newly installed models appear in the selector; models removed from the provider disappear after the refresh.</small>

### How AtM discovers models

<small>AtM does <strong>not</strong> search every SSD, HDD or folder for files ending in <code>.gguf</code>. Model discovery is provider-based:</small>

- <small>AtM asks the provider for the installed model list through <code>GET /api/tags</code>.</small>
- <small>For each reported model, AtM checks capabilities through <code>POST /api/show</code>.</small>
- <small>Only models advertising the <code>completion</code> capability are placed in the AtM model selector.</small>
- <small>Embedding-only models are intentionally excluded.</small>
- <small>Refresh repeats the same discovery process after you install, remove or change models.</small>

<small>A GGUF file copied manually into <code>Downloads</code> or another folder therefore does not become visible to AtM merely because the file exists. The provider must know about the model. Ollama documents importing an external GGUF through a <code>Modelfile</code> and <code>ollama create</code>; see the <a href="https://docs.ollama.com/import">official Ollama import guide</a> and the more detailed <a href="docs/MODEL_GUIDE.md">AtM model guide</a>.</small>

### Finding and choosing an AI model

<small>There is no single model that is appropriate for every machine and every task. Before downloading one, consider its download size, parameter count, quantization, supported languages and tasks, context window, expected memory use and license.</small>

#### Where to look

- <small><a href="https://ollama.com/library">Ollama Library</a> is the simplest starting point when using Ollama. Models and variants can be downloaded directly with <code>ollama pull</code>.</small>
- <small><a href="https://huggingface.co/models?library=gguf">Hugging Face GGUF models</a> provide a much larger catalog for advanced users. A downloaded GGUF must still be imported into a compatible provider before AtM can discover it.</small>
- <small>On Hugging Face, read the model card before downloading. Model cards can document intended use, limitations, evaluation results, languages and licensing.</small>

#### What the common labels mean

| Label | Practical meaning |
| --- | --- |
| `0.8B`, `2B`, `4B`, etc. | Approximate parameter scale. Larger models usually require more storage and memory, but size alone does not determine quality. |
| `Q4_K_M`, `Q8_0`, `BF16`, etc. | Quantization or numerical precision. Lower-precision variants can reduce storage and memory requirements, with an efficiency-versus-fidelity trade-off. |
| Download size | Useful for disk planning, but **not** the same as total RAM or VRAM required while running. Context and provider configuration also use memory. |
| Context window | The amount of input/history the model can handle under the provider configuration. Larger context can increase memory use. |
| License | The model's own usage terms. The MIT license of AtM does not automatically apply to any AI model used with it. |

#### Small models to begin experimenting with

<small>The examples below were checked against the Ollama library on 2026-09-19. They are starting points for experimentation, not a benchmark ranking or a guarantee of performance on a particular computer.</small>

| Example | Approx. download | Example command | Why it may be useful as a first test |
| --- | ---: | --- | --- |
| [Qwen 3.5 0.8B](https://ollama.com/library/qwen3.5/tags) | 1.0 GB | `ollama pull qwen3.5:0.8b` | Very small installation for checking that the complete AtM/provider workflow works. |
| [Qwen 3.5 2B Q4_K_M](https://ollama.com/library/qwen3.5/tags) | 1.9 GB | `ollama pull qwen3.5:2b-q4_K_M` | Small quantized variant with modest disk requirements. |
| [Phi-4 Mini 3.8B Q4_K_M](https://ollama.com/library/phi4-mini/tags) | 2.5 GB | `ollama pull phi4-mini:3.8b-q4_K_M` | Alternative compact model family for comparison. |
| [Qwen 3.5 4B Q4_K_M](https://ollama.com/library/qwen3.5/tags) | 3.4 GB | `ollama pull qwen3.5:4b-q4_K_M` | Larger small-model option when the machine has more memory available. |

<small><strong>Model capability does not automatically become AtM capability.</strong> A model may advertise vision, tool use or reasoning features, but AtM v0.2.2 currently provides text chat only and requests <code>think: false</code> on its default chat path. For more detail on GGUF, quantization, importing models, hardware considerations and model licensing, see <a href="docs/MODEL_GUIDE.md">Choosing, installing and managing AI models</a>.</small>

### Managing models, disk space and memory

<small>Until AtM gains model-management controls of its own, model administration is handled by the provider. With Ollama, the most useful commands are:</small>

| Command | What it does |
| --- | --- |
| `ollama pull MODEL` | Downloads or updates a model. |
| `ollama ls` | Lists models installed in the Ollama model store. |
| `ollama ps` | Shows models currently loaded in memory and whether they are using CPU, GPU or both. |
| `ollama stop MODEL` | Unloads a running model from RAM/VRAM without deleting it from disk. |
| `ollama rm MODEL` | Removes the model from the Ollama model store and frees its disk space. |

<small><strong>Stopping is not deleting.</strong> Use <code>ollama stop</code> when you only want to free memory. Use <code>ollama rm</code> when you want to remove the installed model files.</small>

#### Where Ollama stores models

<small>With the standard Linux installation, Ollama documents its default model store as:</small>

```text
/usr/share/ollama/.ollama/models
```

<small>The storage location can be changed with the <code>OLLAMA_MODELS</code> environment variable. On a standard Linux service installation, the <code>ollama</code> user must have read/write permission to the new directory. This is useful if models should live on a larger SSD or HDD.</small>

<small>Do not normally delete individual blob files by hand. Use the provider's model-removal command so its model store remains internally consistent. The <a href="https://docs.ollama.com/faq">Ollama FAQ</a> documents storage paths, moving the model directory and checking CPU/GPU loading.</small>

### If a model is too slow or too large

<small>A model that downloads successfully may still be a poor fit for a particular machine. Disk size is only one part of the requirement; runtime memory, context length and CPU/GPU offloading also matter.</small>

<small>Check what is actually loaded:</small>

```bash
ollama ps
```

<small>Ollama reports whether a loaded model is using GPU memory, system memory or a mixture of both. If responses are unacceptably slow, a practical sequence is: stop the current model, remove it if you no longer need it, choose a smaller or more strongly quantized variant, download the replacement, and then use Refresh in AtM.</small>

```bash
ollama stop MODEL
ollama rm MODEL
ollama pull SMALLER_MODEL
```

<small>See <a href="docs/TROUBLESHOOTING.md">Troubleshooting</a> for symptom-by-symptom checks.</small>

### Privacy and the local-provider boundary

<small>AtM v0.2.2 connects only to loopback provider addresses on the same machine and sends prompt content when you activate <strong>Send</strong>. Conversation history is held in AtM memory for the current application process and is not persisted by AtM across restarts.</small>

<small>A loopback connection does not by itself prove that every model is local. The external provider decides how a selected model is executed. Modern Ollama versions can also expose cloud features. If strict local-only operation is required, choose a locally installed model and configure the provider accordingly; Ollama documents a local-only mode using <code>OLLAMA_NO_CLOUD=1</code> or <code>disable_ollama_cloud</code> in its server configuration.</small>

### Common first-use problems

| What you see | First checks |
| --- | --- |
| **Ollama not found** | Run `ollama ls`; confirm the provider is running on `127.0.0.1:11434` or the supported `11435` fallback. |
| **No chat models** | Run `ollama ls`; install a chat/completion model; then press Refresh in AtM. |
| **A downloaded `.gguf` does not appear** | A raw file is not enough. Import/register it with the provider first. |
| **A new model does not appear while AtM is open** | Press **Refresh models** after the provider finishes installing the model. |
| **Responses are very slow** | Run `ollama ps`; try a smaller or more strongly quantized model if CPU/RAM use is dominating or memory is tight. |
| **Disk space is low** | Use `ollama ls` to identify installed models and `ollama rm MODEL` to remove models you no longer need. |

<small>The full diagnostic guide is in <a href="docs/TROUBLESHOOTING.md">docs/TROUBLESHOOTING.md</a>.</small>

### What AtM does today

- <small>discovers a supported local Ollama-compatible provider;</small>
- <small>automatically enumerates provider-managed models;</small>
- <small>filters the list to models advertising the <code>completion</code> capability;</small>
- <small>lets you switch among compatible detected models;</small>
- <small>refreshes model discovery without restarting the application;</small>
- <small>streams assistant responses as they are generated;</small>
- <small>keeps multi-turn conversation context in memory for the current session;</small>
- <small>follows the desktop light/dark appearance;</small>
- <small>runs as a GTK 4 / Granite application packaged for the elementary OS 8 Flatpak runtime.</small>

### What the current public release does not yet expose

<small>The public v0.2.2 application does not yet expose the repository-aware backend work that is being developed on `main`. In the released application, users cannot yet:</small>

- <small>download, import, update or delete AI models through AtM;</small>
- <small>install, start, stop or update the external provider through AtM;</small>
- <small>configure provider host/port, authentication or TLS in the UI;</small>
- <small>persist conversations across restarts;</small>
- <small>select, ingest or retrieve scientific repositories through the released GTK conversation flow;</small>
- <small>receive repository-grounded citations or provenance in the released chat UI;</small>
- <small>execute or simulate EWD, CBD or RMD;</small>
- <small>turn ordinary AI chat output into an authoritative scientific-model result.</small>

<small>These are release boundaries, not claims that no development backend exists. See <a href="STATUS.md">Application status</a> for the separate development-`main` state.</small>

<details>
<summary><strong>Scientific-model roadmap and boundary</strong></summary>

<small>AtM is intended to work with scientific dynamical-model repositories without replacing them. The planned initial suite includes EWD, CBD and RMD. Each source repository remains canonical for its documentation, code, data, assumptions, provenance, validation and release boundaries.</small>

<small>Development `main` now contains an unreleased repository-aware GTK path for the fixed EWD/CBD/RMD suite: repository lifecycle controls, per-chat frozen repository/model context, deterministic retrieval, grounded generation, multi-chat navigation and on-demand citation provenance. This development functionality is not part of the public v0.2.2 release. Repository evidence, actual scientific-model outputs and AI-generated interpretation remain distinct concepts, and ordinary local chat in v0.2.2 must not be interpreted as repository-grounded scientific analysis.</small>

</details>

### Supported platform and compatibility

<small>The packaged baseline is <strong>elementary OS 8 / Linux</strong> using <code>io.elementary.Platform//8</code>. Other Linux environments may work when their Flatpak and display stack are compatible, but they are not part of the documented packaged baseline.</small>

<small>AtM currently expects the Ollama API behavior required for <code>/api/tags</code>, <code>/api/show</code> and <code>/api/chat</code>. GPU/CPU execution belongs to the provider; AtM itself contains no inference engine and no vendor-specific GPU inference code. See <a href="docs/DEPENDENCIES_AND_COMPATIBILITY.md">Dependencies and compatibility</a> for the precise contract.</small>

<details>
<summary><strong>Development build</strong></summary>

<small>The dependencies below are for contributors building AtM from source. They are <strong>not</strong> prerequisites for installing the release Flatpak.</small>

#### Native build dependencies

```bash
sudo apt install meson valac libgtk-4-dev libgranite-7-dev libsoup-3.0-dev libjson-glib-dev
meson setup build --prefix=/usr
meson compile -C build
./build/io.github.laurentiustaicu.ask_the_model
```

#### Flatpak development build

```bash
sudo apt install flatpak-builder
flatpak install -y appcenter io.elementary.Platform//8 io.elementary.Sdk//8
flatpak-builder flatpak-build io.github.laurentiustaicu.ask_the_model.yml --user --install --force-clean
flatpak run io.github.laurentiustaicu.ask_the_model
```

<small>The elementary SDK is a build-time dependency. It is not a manually installed prerequisite for someone installing the published release bundle.</small>

</details>

<details>
<summary><strong>Project status and build health</strong></summary>

<p><a href="https://github.com/LaurentiuStaicu/ask-the-model/actions/workflows/flatpak.yml"><img alt="Flatpak build status" src="https://img.shields.io/github/actions/workflow/status/LaurentiuStaicu/ask-the-model/flatpak.yml?branch=main&event=push&style=flat-square&label=Flatpak%20build&color=707070"></a></p>

<small>This badge is a continuous-integration indicator: it shows whether the current Flatpak workflow on <code>main</code> is passing. It is placed here rather than in the project header because build health is developer/project-maintenance information, not a requirement for understanding how to use AtM.</small>

</details>

### Documentation

- <small><a href="docs/MODEL_GUIDE.md">Choosing, installing and managing AI models</a> — model sources, GGUF, quantization, storage, imports and local-only considerations.</small>
- <small><a href="docs/TROUBLESHOOTING.md">Troubleshooting</a> — provider detection, missing models, performance, storage and installation problems.</small>
- <small><a href="STATUS.md">Application status</a> — implemented capabilities and current release boundary.</small>
- <small><a href="docs/DEPENDENCIES_AND_COMPATIBILITY.md">Dependencies and compatibility</a> — provider API contract, sandbox and platform details.</small>
- <small><a href="docs/ARCHITECTURE.md">Architecture boundary</a> — relationship between AtM and scientific-model repositories.</small>
- <small><a href="docs/INTERFACE_DESIGN_REQUIREMENTS.md">Interface design requirements</a>.</small>
- <small><a href="docs/TERMINOLOGY.md">Terminology</a>.</small>
- <small><a href="CHANGELOG.md">Changelog</a> and <a href="releases/">release notes</a>.</small>
- <small><a href="CITATION.cff">Citation metadata</a>.</small>

### Help and project maintenance

<small>If something does not work, start with <a href="docs/TROUBLESHOOTING.md">Troubleshooting</a>. It covers provider detection, missing models, raw GGUF imports, slow inference, memory use, disk-space problems and Flatpak startup checks.</small>

<small>If the problem remains reproducible, open a <a href="https://github.com/LaurentiuStaicu/ask-the-model/issues">GitHub issue</a> and include the AtM version, Linux distribution/session, provider version, model name and the exact steps needed to reproduce the problem. See <a href=".github/SUPPORT.md">Support</a> for the correct issue route and <a href=".github/CONTRIBUTING.md">Contributing</a> before proposing repository changes. Do not include private prompts, sensitive local information or vulnerability details in a public issue.</small>

<small>Ask the Model is maintained in this repository by <a href="https://github.com/LaurentiuStaicu">LaurentiuStaicu</a>. Release history, current boundaries and planned scientific-repository integration are documented in <a href="CHANGELOG.md">CHANGELOG.md</a>, <a href="STATUS.md">STATUS.md</a> and the <a href="releases/">release notes</a>.</small>

### License

<small>Ask the Model is released under the <a href="LICENSE"><strong>MIT License</strong></a>. See the repository <a href="LICENSE">LICENSE</a> file for the complete terms.</small>

<small>AI models used with AtM are separate artifacts and may use MIT, Apache, community, research, OpenRAIL, Gemma, Llama or other licensing terms. Always check the model page or model card before downloading, redistributing or using a model in a context where its license matters. The MIT license of AtM does not override a model's own license.</small>
