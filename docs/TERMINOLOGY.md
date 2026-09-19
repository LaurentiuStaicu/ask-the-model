# Terminology and language policy

## Purpose

Ask the Model (AtM) works with two different kinds of things that are both commonly called “models”. To prevent ambiguity in the user interface, documentation, source code and repository metadata, AtM uses distinct canonical terms.

## Canonical terminology

| Concept | English UI | Romanian UI | Internal/documentation meaning |
| --- | --- | --- | --- |
| EWD, CBD, RMD and compatible projects | **Repository** / **Repositories** | **Repository** / **Repository-uri** | A scientific dynamical-model repository used as conversational context |
| Local language model used to generate an answer | **AI model** | **Model AI** | The selected local inference model, e.g. an Ollama-hosted LLM |
| Local service exposing AI models | **AI provider** | **Furnizor AI** | The runtime/API provider, initially Ollama-compatible |
| A chat session | **Conversation** | **Conversație** | Ordered user/assistant messages with recorded repository and AI-model context |
| Material retrieved from repositories | **Sources** | **Surse** | Files or passages supplied as grounded context for a response |

## Ambiguity rule

Do not use the bare word **model** in AtM interface copy or project documentation when the referent could be ambiguous.

Use:

- **Repository** when referring to EWD, CBD, RMD or another scientific dynamical-model project selected for discussion.
- **AI model** when referring to the local LLM that generates conversational responses.
- The full scientific term **dynamical model** only when discussing the scientific nature of a repository rather than naming the UI control.

Examples:

- Good: “Select repositories”
- Good: “AI model: ministral-3:3b”
- Good: “This repository contains a scientific dynamical model”
- Avoid: “Select models” when it could mean either repositories or AI models

## Language policy

AtM is bilingual:

- English (`en`) is the default interface language.
- Romanian (`ro`) is the second supported interface language.
- Language choice applies to AtM interface strings, not to the language in which the user is allowed to chat. Conversations may use any language supported by the selected AI model.
- All user-visible strings must be translatable; new UI copy must not be hard-coded outside the localization system.
- Terminology must remain semantically parallel between English and Romanian.

The first language setting should expose:

- **English** — default
- **Română**

## Compact selector labels

For the minimalist header:

- Repository selector: **Repositories** / **Repository-uri**
- AI selector: **AI model** / **Model AI**

The repository selector may contain one or more selections. Its closed state should summarize values rather than repeat the label when space is limited, e.g. `EWD + RMD` or `3 repositories`.

The AI-model selector represents one active local AI model for the current request.
