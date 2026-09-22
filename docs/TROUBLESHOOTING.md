# Troubleshooting Ask the Model

This guide covers the most common problems in the AtM v0.4.0 local-chat setup. Work through the checks in order. The reference provider in the examples is Ollama.

## Quick diagnostic sequence

Before changing configuration, run these commands:

```bash
flatpak --version
ollama --version
ollama ls
ollama ps
```

Then start AtM from a terminal:

```bash
flatpak run io.github.laurentiustaicu.ask_the_model
```

This gives you a basic picture of whether Flatpak is available, whether the provider command exists, which models are installed, which models are loaded and what AtM reports during provider/model discovery.

## AtM shows “Ollama not found”

AtM v0.4.0 probes these endpoints in order:

1. `http://127.0.0.1:11434`
2. `http://127.0.0.1:11435`

The first endpoint is the standard Ollama location. The second is a compatibility fallback for managed Ollama-compatible setups.

### Check the provider

```bash
ollama ls
```

If this reports a connection error, the provider is not ready even before AtM is involved.

On a standard Linux Ollama service installation, inspect the service:

```bash
systemctl status ollama
```

Restart it if necessary:

```bash
sudo systemctl restart ollama
```

You can also query the standard API endpoint directly:

```bash
curl http://127.0.0.1:11434/api/tags
```

A JSON response containing a `models` array confirms that the API is reachable at the standard endpoint.

If your provider is intentionally using port `11435`, confirm that it exposes the required Ollama-compatible API there.

## AtM finds the provider but shows “No chat models”

First list the provider-managed models:

```bash
ollama ls
```

If the list is empty, download a chat model:

```bash
ollama pull qwen3.5:2b-q4_K_M
```

Then click **Refresh models** in AtM.

If `ollama ls` shows models but AtM still shows no chat models, AtM may be filtering them because they do not advertise the `completion` capability. Embedding-only models are intentionally excluded.

## A model is installed but does not appear in AtM

Check each condition in this order:

1. Confirm the model appears in `ollama ls`.
2. Confirm AtM is connected to the same provider instance that owns that model.
3. Press **Refresh models**.
4. Confirm the model is intended for chat/text completion rather than embeddings only.

AtM does not maintain its own model database. It rebuilds the selectable list from the provider.

## I downloaded a `.gguf`, but AtM cannot see it

A raw GGUF file stored in `Downloads`, on another SSD, or in an arbitrary directory is not automatically a provider-managed model.

For Ollama, create a `Modelfile`:

```text
FROM /path/to/file.gguf
```

Then import it:

```bash
ollama create my-model
ollama ls
```

After it appears in `ollama ls`, press **Refresh models** in AtM.

Official reference: [Ollama model import](https://docs.ollama.com/import).

## I installed a new model while AtM was open

AtM performs discovery at startup and when you explicitly refresh.

After the provider finishes downloading/importing the new model, click the circular **Refresh models** button in the AtM title bar. The selector should update without restarting the application.

## The model is extremely slow

Check where the model is loaded:

```bash
ollama ps
```

Ollama reports whether the model is using GPU, CPU or a mixture of both. A model that cannot fit efficiently in available GPU memory may rely more heavily on system RAM/CPU and become much slower.

Practical options:

1. Try a smaller parameter scale.
2. Try a smaller quantized variant such as a Q4 model instead of a much larger precision variant.
3. Reduce unnecessary context requirements in the provider configuration.
4. Stop other loaded models.
5. Remove models you do not need.

To unload a model from memory without deleting it:

```bash
ollama stop MODEL
```

For more background, see [Choosing, installing and managing AI models](MODEL_GUIDE.md).

## RAM or VRAM remains occupied after a chat

Providers may keep a model loaded to make the next request faster. Ollama currently documents a default keep-alive period of approximately five minutes.

Unload it immediately with:

```bash
ollama stop MODEL
```

This does not delete the model from disk.

## I need to free disk space

List installed models:

```bash
ollama ls
```

Remove one you no longer need:

```bash
ollama rm MODEL
```

After removal, press **Refresh models** in AtM.

Do not normally delete random blobs from the Ollama model directory by hand.

## Where did the model files go?

The standard Ollama paths currently documented by Ollama are:

| Operating system | Default location |
| --- | --- |
| Linux | `/usr/share/ollama/.ollama/models` |
| macOS | `~/.ollama/models` |
| Windows | `C:\Users\%username%\.ollama\models` |

For AtM users, Linux is the relevant packaged baseline.

If you configured `OLLAMA_MODELS`, the actual model store may be elsewhere.

## I moved the model store and Ollama no longer sees the models

On Linux with the standard service installation, the `ollama` service user needs read/write access to the selected model directory.

Check the `OLLAMA_MODELS` configuration and permissions. The Ollama FAQ documents this ownership pattern:

```bash
sudo chown -R ollama:ollama <directory>
```

Restart the provider after changing its environment:

```bash
sudo systemctl restart ollama
```

Then verify:

```bash
ollama ls
```

Do not delete the old store until the provider sees the expected models from the new location.

## Flatpak says the AtM bundle cannot be found

Make sure your terminal is in the directory containing the downloaded file or provide its full path.

For example, if the bundle is in `~/Downloads`:

```bash
flatpak install --user ~/Downloads/AskTheModel.flatpak
```

The current release bundle is available from:

[Latest AskTheModel.flatpak](https://github.com/LaurentiuStaicu/ask-the-model/releases/latest/download/AskTheModel.flatpak)

## AtM is installed but will not launch

Run it from a terminal so that any startup message is visible:

```bash
flatpak run io.github.laurentiustaicu.ask_the_model
```

Confirm the application is installed:

```bash
flatpak list --app | grep ask_the_model
```

If you installed an older local build and are unsure which copy is active, inspect:

```bash
flatpak --user info io.github.laurentiustaicu.ask_the_model
```

## AtM replies with a provider HTTP error

An HTTP error means AtM reached the provider but the provider rejected or could not complete the request.

Check:

1. that the selected model still exists;
2. that the provider can run the same model directly;
3. that enough RAM/VRAM is available;
4. that the provider implements the required `/api/chat` behavior;
5. provider logs for a more specific error.

Try the model directly with Ollama:

```bash
ollama run MODEL
```

If direct provider execution also fails, fix the provider/model problem before debugging AtM.

## My conversation disappeared after restarting AtM

This is expected in v0.4.0. Conversation history exists only in application memory for the current AtM process. Persistent conversation storage is not implemented yet.

## Repository Refresh shows an update

Repository Refresh compares the selected repository's tracked remote branch with the exact SHA of the current validated local snapshot.

If Refresh shows an update, this is not an error. Use **Download/Update** to fetch and validate the new exact revision.

AtM does not overwrite the old snapshot in place. A successful update creates a new immutable snapshot/index pair and may retain the previous one for rollback or active-chat pinning.

## Repository Download/Update fails

Start AtM from a terminal:

```bash
flatpak run io.github.laurentiustaicu.ask_the_model
```

Then repeat Refresh and Download/Update.

Useful checks:

1. confirm normal Internet access to GitHub;
2. confirm the repository selector includes the intended repository;
3. read the terminal line naming the repository, remote SHA and any validation error;
4. verify that the application still reports the previous valid snapshot as usable if one existed.

Do not manually move partial content from `Repositories/.staging` into a snapshot directory. Promotion to READY is intentionally application-controlled.

## A grounded answer has no numbered sources

Check that:

1. at least one repository was selected **before the first Send**;
2. the selected repository reached READY;
3. the question is related to content that exists in the selected repository scope;
4. you are not expecting a chat that started with zero repositories to acquire repository scope later.

Repository scope is frozen on the first Send. Create a new chat to change it.

## The model or repository selector is locked

This is expected after the first Send.

Each chat freezes its selected AI-model identity and exact repository snapshot set so later provider/repository changes cannot silently alter the evidence basis of an existing transcript.

Create a new chat to choose a different model or repository scope.

## A numbered source opens unexpected text

Open the source-detail window and record:

- repository acronym/version;
- snapshot SHA;
- source file and locator;
- Source ID;
- the visible excerpt.

Then use **Open immutable source** and verify that GitHub opens the same commit SHA and locator.

If those identifiers disagree, report it through the **Repository / grounding / provenance** issue form. Do not report only the generated prose; provenance identifiers are required to diagnose retrieval/citation problems.

## Old repository snapshots remain after an update

This can be expected.

Validated snapshots are immutable. AtM may retain the previous snapshot/index pair for safe rollback or because an active chat is pinned to it.

The v0.4.0 compact header does not yet expose full snapshot-history/removal management.

## I expected AtM to download or delete a model

This is not implemented in v0.4.0.

For now, use provider commands:

```bash
ollama pull MODEL
ollama ls
ollama stop MODEL
ollama rm MODEL
```

Then use **Refresh models** in AtM.

Model download/delete controls are a future product capability, not something hidden in the current interface.

## I need strict local-only inference

AtM connects to loopback provider addresses only, but the provider may itself expose local and cloud-backed capabilities.

With current Ollama versions, strict local-only operation can be reinforced by disabling Ollama cloud features. Ollama documents:

```text
OLLAMA_NO_CLOUD=1
```

or `disable_ollama_cloud` in `~/.ollama/server.json`.

Choose an actually installed local model and review the provider configuration if privacy or offline operation is important.

Official reference: [Ollama FAQ](https://docs.ollama.com/faq).

## The Flatpak workflow badge is red

The build badge in the README reports the current GitHub Actions Flatpak workflow on `main`. It is a project-maintenance signal, not an indicator that a previously installed AtM copy has suddenly stopped working.

Open the badge or the repository Actions tab to inspect the failing run.

## GitHub shows an old red Deployment

The repository previously experimented with publishing the development Flatpak repository through GitHub Pages. That historical Pages deployment failed because GitHub Pages had not been enabled, and the workflow was subsequently replaced with direct publication to the `flatpak-repo` branch.

The current build/release workflow no longer uses the `github-pages` environment. A historical red Deployment therefore does not describe the current AtM v0.4.0 build state.

Repository administrators can clean up obsolete deployment/environment history from GitHub settings or the GitHub Deployments API where appropriate.

## Collect useful information before reporting a problem

If the problem remains reproducible, collect:

- AtM release version;
- Linux distribution and desktop session (Wayland/X11);
- Flatpak version;
- provider name/version;
- provider endpoint/port;
- model name and variant;
- output of `ollama ls`;
- output of `ollama ps` while the problem is happening;
- terminal output from `flatpak run io.github.laurentiustaicu.ask_the_model`;
- for repository/grounding problems, repository acronym, snapshot SHA and Source ID/locator when available;
- the exact steps needed to reproduce the problem.

Do not include private prompts, credentials or sensitive local paths unless they are necessary and you intentionally want to share them.

Issues can be reported through the [AtM GitHub Issues page](https://github.com/LaurentiuStaicu/ask-the-model/issues).

## Related documentation

- [README / first-time setup](../README.md#start-here-first-time-setup)
- [Interface guide](USER_INTERFACE_GUIDE.md)
- [Model guide](MODEL_GUIDE.md)
- [Dependencies and compatibility](DEPENDENCIES_AND_COMPATIBILITY.md)
- [Development guide](DEVELOPMENT_GUIDE.md)
- [Application status](../STATUS.md)
- [Ollama CLI](https://docs.ollama.com/cli)
- [Ollama FAQ](https://docs.ollama.com/faq)
- [Ollama import guide](https://docs.ollama.com/import)
