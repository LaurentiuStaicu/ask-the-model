<p align="center">
  <img src="assets/icon.png" alt="Ask the Model icon" width="112">
</p>

<h2 align="center">Ask the Model (AtM)</h2>

<p align="center">
  <a href="https://github.com/LaurentiuStaicu/ask-the-model/releases/latest"><img alt="Version: 0.5.0" src="https://img.shields.io/github/v/tag/LaurentiuStaicu/ask-the-model?sort=semver&style=flat-square&label=release&color=333333"></a>
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-707070?style=flat-square"></a>
  <a href="#supported-platform-and-compatibility"><img alt="Linux / Flatpak" src="https://img.shields.io/badge/platform-Linux%20%2F%20Flatpak-a0a0a0?style=flat-square"></a>
</p>

<p align="center">
  <a href="https://github.com/LaurentiuStaicu/ask-the-model/releases/latest/download/AskTheModel.flatpak"><img alt="Download Flatpak" src="https://img.shields.io/badge/download-Flatpak-333333?style=flat-square&logo=flatpak&logoColor=white"></a>
  <a href="#start-here-first-time-setup"><img alt="Getting started" src="https://img.shields.io/badge/read-Getting%20started-707070?style=flat-square"></a>
  <a href="docs/USER_INTERFACE_GUIDE.md"><img alt="Interface guide" src="https://img.shields.io/badge/read-Interface%20guide-a0a0a0?style=flat-square"></a>
</p>

<p align="center"><small><strong>A local-first desktop interface for local AI chat and repository-grounded exploration of scientific dynamical models with exact, inspectable provenance.</strong></small></p>

<p align="center"><small><a href="#start-here-first-time-setup">First-time setup</a> · <a href="#repository-grounded-scientific-model-chat">Repository-grounded chat</a> · <a href="#save-conversations-for-later">Saved conversations</a> · <a href="docs/USER_INTERFACE_GUIDE.md">Interface guide</a> · <a href="docs/TROUBLESHOOTING.md">Troubleshooting</a> · <a href="STATUS.md">Project status</a></small></p>

---

### What is Ask the Model?

<small>Ask the Model (AtM) is a local-first desktop interface for two related workflows: ordinary chat with locally managed AI models, and repository-grounded exploration of the fixed EWD/CBD/RMD scientific-model suite.</small>

<p align="center"><code>You → Ask the Model → local provider → AI model</code></p>

<p align="center"><code>You → Ask the Model → validated repository snapshot → retrieval/provenance → AI model</code></p>

<small><strong>Ask the Model</strong> provides the interface and retrieval/provenance layer. A <strong>local provider</strong>, such as Ollama, loads and runs AI models. The <strong>AI model</strong> remains a separate artifact that must currently be downloaded and managed through the provider rather than AtM.</small>

<small>Version 0.5.0 adds explicit <strong>Save to History</strong> conversation archiving with exact-context restore and deterministic local JSON mirrors for saved conversations. It also promotes the hardened SQLite repository/control-state and conversation-state work completed after v0.4.0, while retaining offline startup qualification and deterministic local snapshot-integrity seals. EWD, CBD and RMD remain authoritative for their own code, data, assumptions, provenance and validation.</small>

### Repository-grounded scientific-model chat

<small>Select any combination of EWD, CBD and RMD before the first Send. Refresh resolves each tracked branch to an exact Git SHA. Download/Update validates an immutable local snapshot and builds a deterministic per-snapshot retrieval index. The first Send freezes the model/repository identity for that chat.</small>

<small>Grounded answers expose numbered source references. Opening a source shows repository/version, exact snapshot SHA, file/locator, logical source ID, readable excerpt and an immutable GitHub permalink where appropriate. Repository evidence is current-turn data and is not accumulated permanently into ordinary provider conversation history.</small>

### Save conversations for later

<small>A conversation is saved only when you activate the per-tab <strong>Save to History</strong> control. Saved conversations appear in History and can later be reopened with their transcript, model identity, exact repository generation/version/SHA pins and citation provenance intact. AtM requalifies that exact historical context before allowing continuation; if it is unavailable, the saved conversation remains readable but view-only rather than being silently repinned.</small>

<small>Saved conversations are also mirrored automatically as deterministic JSON files under <code>~/Ask the Model/Conversation Exports</code>. Closing an unsaved chat with <strong>X</strong> does not add it to History. History provides <strong>Open</strong> and separately confirmed <strong>Delete permanently</strong>; permanent deletion removes both the archived conversation and its managed JSON mirror.</small>

### Before you begin

<small>You need four things for a normal first run:</small>

- <small><strong>Linux with Flatpak</strong> — used to install and run AtM.</small>
- <small><strong>A local Ollama-compatible provider</strong> — the software that actually loads and runs AI models.</small>
- <small><strong>At least one chat-capable AI model</strong> — downloaded separately through the provider; AtM does not currently download it for you.</small>
- <small><strong>Enough free disk space and runtime memory</strong> — model downloads commonly occupy gigabytes, and running a model also requires RAM and, where available, GPU memory.</small>

<small>You also need Internet access for the initial AtM/provider/model downloads and for explicit repository Refresh/Download/Update. Once a genuinely local model is installed, ordinary chat talks to the provider through the local loopback interface.</small>

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

<small>The Ollama library currently lists this variant at about 1.9 GB. This is an example, not a claim that it is the best model for every computer or task. See <a href="docs/MODEL_GUIDE.md">Choosing, installing and managing AI models</a> before installing larger models.</small>

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

<small><strong>Expected result:</strong> the model selector in the application header shows a compatible AI model instead of <code>No chat models detected</code>, and you can send a prompt. A brief scan status such as <code>1 model found</code> may appear while discovery finishes.</small>

#### 6. Download more models later and use Refresh

<small>You can install another model at any time with your provider. If AtM is already open when the download finishes, click the circular <strong>Refresh models</strong> button beside the model selector in the application header. AtM repeats model discovery and updates the selector; you do not need to add the model to AtM manually.</small>

<small><strong>Expected result:</strong> compatible newly installed models appear in the selector; models removed from the provider disappear after the refresh.</small>

#### 7. Optional: use repository-grounded chat

<small>Open the repository selector and choose EWD, CBD, RMD or any combination. Press <strong>Refresh repositories</strong> to compare your local state with each tracked remote SHA. If a repository is not installed or has changed, use <strong>Download/Update</strong>.</small>

<small>Validated snapshots are stored under <code>~/Ask the Model/Repositories</code>. After the first Send, the selected AI model and repository snapshot set are frozen for that chat; create a new chat to change either identity.</small>

<small><strong>Expected result:</strong> the status strip reaches READY, a grounded answer can show numbered source references, and opening a source exposes exact immutable provenance.</small>

### Model discovery and management

<small>AtM does <strong>not</strong> search every SSD, HDD or folder for files ending in <code>.gguf</code>. Model discovery is provider-based:</small>

- <small>AtM asks the provider for the installed model list through <code>GET /api/tags</code>.</small>
- <small>For each reported model, AtM checks capabilities through <code>POST /api/show</code>.</small>
- <small>Only models advertising the <code>completion</code> capability are placed in the AtM model selector; embedding-only models are excluded.</small>
- <small>Refresh repeats discovery after models are installed, removed or changed.</small>

<small>A raw GGUF file does not become visible to AtM merely because it exists on disk; it must first be registered with a compatible provider. For model sources, GGUF import, parameter scales, quantization, context windows, starter examples, storage, CPU/GPU checks and Ollama management commands, see <a href="docs/MODEL_GUIDE.md">Choosing, installing and managing AI models</a>.</small>

<small><strong>Model capability does not automatically become AtM capability.</strong> AtM v0.5.0 currently provides text chat and repository-grounded text retrieval. Image input and tool-calling controls are not part of this release, and the default chat path requests <code>think: false</code>.</small>

<small>If a model is too slow, consumes too much memory or does not appear in AtM, use <a href="docs/TROUBLESHOOTING.md">Troubleshooting</a> for symptom-by-symptom checks rather than treating model size or download success as proof of compatibility.</small>

### Privacy and the local-provider boundary

<small>AtM v0.5.0 connects only to loopback provider addresses on the same machine and sends prompt content when you activate <strong>Send</strong>. Working chats are not saved implicitly: only conversations explicitly sent to <strong>History</strong> persist across restarts, with managed JSON mirrors under <code>~/Ask the Model/Conversation Exports</code>. Repository-grounded evidence comes from validated local snapshots; network access is used for explicit repository Refresh/Download/Update and for immutable GitHub links opened by the user.</small>

<small>A loopback connection does not by itself prove that every model is local. The external provider decides how a selected model is executed. Modern Ollama versions can also expose cloud features. If strict local-only operation is required, choose a locally installed model and configure the provider accordingly; Ollama documents a local-only mode using <code>OLLAMA_NO_CLOUD=1</code> or <code>disable_ollama_cloud</code> in its server configuration.</small>

### Common first-use problems

| What you see | First checks |
| --- | --- |
| **Ollama not found** | Run `ollama ls`; confirm the provider is running on `127.0.0.1:11434` or the supported `11435` fallback. |
| **No chat models** | Run `ollama ls`; install a chat/completion model; then press Refresh in AtM. |
| **A downloaded `.gguf` does not appear** | A raw file is not enough. Import/register it with the provider first. |
| **A new model does not appear while AtM is open** | Press **Refresh models** after the provider finishes installing the model. |
| **A selected repository is not READY or shows an update** | Press **Refresh repositories**; if the exact remote SHA differs or the repository is missing, use **Download/Update**. |
| **A grounded answer has no `Sources:` references** | Confirm the repository was selected and READY **before the first Send**. Repository scope is frozen for that chat; create a new chat to change it. |
| **Responses are very slow** | Run `ollama ps`; try a smaller or more strongly quantized model if CPU/RAM use is dominating or memory is tight. |
| **Disk space is low** | Use `ollama ls` to identify installed models and `ollama rm MODEL` to remove models you no longer need. |

<small>The full diagnostic guide is in <a href="docs/TROUBLESHOOTING.md">docs/TROUBLESHOOTING.md</a>.</small>

### What AtM does today

- <small>discovers a supported local Ollama-compatible provider and completion-capable provider-managed models;</small>
- <small>refreshes model discovery without restarting;</small>
- <small>streams assistant responses in independent multi-chat tabs;</small>
- <small>saves only explicitly selected conversations to History and reopens them through exact model/repository-context qualification;</small>
- <small>maintains deterministic managed JSON mirrors for saved conversations under <code>~/Ask the Model/Conversation Exports</code>;</small>
- <small>selects any combination of the fixed EWD/CBD/RMD repository catalog before the first Send;</small>
- <small>resolves repository Refresh to exact Git SHAs and performs explicit validated Download/Update;</small>
- <small>stores immutable validated snapshots under <code>~/Ask the Model/Repositories</code>;</small>
- <small>builds deterministic per-snapshot SQLite/FTS5 retrieval indexes;</small>
- <small>retrieves exact identifiers, structured entities/relations, lexical evidence and tabular rows;</small>
- <small>pins AI-model and repository snapshot identity per chat on the first Send;</small>
- <small>constructs current-turn repository grounding without accumulating evidence blocks into normal provider history;</small>
- <small>shows compact numbered citations with exact repository/version/SHA/source/locator provenance and immutable source links;</small>
- <small>follows desktop light/dark appearance and runs as a GTK 4 / Granite Flatpak for the elementary OS 8 runtime.</small>

### What v0.5.0 still does not do

- <small>download, import, move, update or delete AI model files through AtM;</small>
- <small>install, start, stop or update the external provider through AtM;</small>
- <small>configure provider host/port, authentication or TLS in the UI;</small>
- <small>import saved-conversation JSON archives;</small>
- <small>provide retention-policy or bulk History management;</small>
- <small>accept arbitrary unreviewed repository origins beyond the fixed EWD/CBD/RMD catalog;</small>
- <small>execute or simulate EWD, CBD or RMD;</small>
- <small>autonomously modify scientific repositories;</small>
- <small>turn generated text into an authoritative scientific-model result without repository-grounded provenance or actual model execution.</small>

<details>
<summary><strong>Scientific-model boundary</strong></summary>

<small>AtM is an interface and retrieval/provenance layer. The source repositories remain canonical for their documentation, code, data, assumptions, provenance, validation and release boundaries. A repository marked READY has passed AtM compatibility/integrity checks; READY is not a scientific certification.</small>

<small>Future work may add broader repository management, provider support, conversation-archive import/retention tools, semantic retrieval or explicit scientific-model execution. Such work must preserve exact provenance and remain distinct from generated interpretation.</small>

</details>

### Supported platform and compatibility

<small>The packaged baseline is <strong>elementary OS 8 / Linux</strong> using <code>io.elementary.Platform//8</code>. Other Linux environments may work when their Flatpak and display stack are compatible, but they are not part of the documented packaged baseline.</small>

<small>AtM currently expects the Ollama API behavior required for <code>/api/tags</code>, <code>/api/show</code> and <code>/api/chat</code>. GPU/CPU execution belongs to the provider; AtM itself contains no inference engine and no vendor-specific GPU inference code. See <a href="docs/DEPENDENCIES_AND_COMPATIBILITY.md">Dependencies and compatibility</a> for the precise contract.</small>

<details>
<summary><strong>Development build</strong></summary>

<small>The dependencies below are for contributors building AtM from source. They are <strong>not</strong> prerequisites for installing the release Flatpak.</small>

#### Native build dependencies

```bash
sudo apt install meson valac libgtk-4-dev libgranite-7-dev libsoup-3.0-dev libjson-glib-dev libsqlite3-dev libarchive-dev libyaml-dev
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

#### User documentation

- <small><a href="docs/USER_INTERFACE_GUIDE.md">User interface guide</a> — controls, repository lifecycle states, chat behavior and source-detail behavior.</small>
- <small><a href="docs/MODEL_GUIDE.md">Choosing, installing and managing AI models</a> — model sources, GGUF, quantization, storage, imports and local-only considerations.</small>
- <small><a href="docs/TROUBLESHOOTING.md">Troubleshooting</a> — provider detection, models, performance, repositories, storage and installation problems.</small>
- <small><a href="STATUS.md">Application status</a> — implemented capabilities and current release boundary.</small>

#### Developer / architecture documentation

- <small><a href="docs/DEVELOPMENT_GUIDE.md">Development guide</a> — code map, extension points, invariants, tests and release workflow.</small>
- <small><a href="docs/DEPENDENCIES_AND_COMPATIBILITY.md">Dependencies and compatibility</a> — provider API contract, sandbox and platform details.</small>
- <small><a href="docs/ARCHITECTURE.md">Application architecture</a> and <a href="docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md">repository/retrieval architecture</a>.</small>
- <small><a href="docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md">Repository/retrieval acceptance gates</a>.</small>
- <small><a href="docs/INTERFACE_DESIGN_REQUIREMENTS.md">Interface design requirements</a> and <a href="docs/TERMINOLOGY.md">terminology</a>.</small>
- <small><a href=".github/CONTRIBUTING.md">Contributing</a>, <a href=".github/SUPPORT.md">support</a>, <a href="CHANGELOG.md">changelog</a>, <a href="releases/">release notes</a> and <a href="CITATION.cff">citation metadata</a>.</small>

### Help and project maintenance

<small>If something does not work, start with <a href="docs/TROUBLESHOOTING.md">Troubleshooting</a>. It covers provider detection, missing models, raw GGUF imports, slow inference, memory use, disk-space problems and Flatpak startup checks.</small>

<small>If the problem remains reproducible, open a <a href="https://github.com/LaurentiuStaicu/ask-the-model/issues">GitHub issue</a> and include the AtM version, Linux distribution/session, provider version, model name and the exact steps needed to reproduce the problem. See <a href=".github/SUPPORT.md">Support</a> for the correct issue route and <a href=".github/CONTRIBUTING.md">Contributing</a> before proposing repository changes. Do not include private prompts, sensitive local information or vulnerability details in a public issue.</small>

<small>Ask the Model is maintained in this repository by <a href="https://github.com/LaurentiuStaicu">LaurentiuStaicu</a>. Release history, current boundaries, repository/retrieval architecture and future extension points are documented in <a href="CHANGELOG.md">CHANGELOG.md</a>, <a href="STATUS.md">STATUS.md</a>, <a href="docs/DEVELOPMENT_GUIDE.md">the development guide</a> and the <a href="releases/">release notes</a>.</small>

### License

<small>Ask the Model is released under the <a href="LICENSE"><strong>MIT License</strong></a>. See the repository <a href="LICENSE">LICENSE</a> file for the complete terms.</small>

<small>AI models used with AtM are separate artifacts and may use MIT, Apache, community, research, OpenRAIL, Gemma, Llama or other licensing terms. Always check the model page or model card before downloading, redistributing or using a model in a context where its license matters. The MIT license of AtM does not override a model's own license.</small>
