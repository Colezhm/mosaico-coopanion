## 0.3.0

- Needs Cortico 0.2.0 (provider contract 6): the module's panels are declared by `webUi()`, and the sign-in methods of the `chatgpt` module are reached under `/api/webui/`.
- Self-description: `describe` on the `coo` and `chatgpt` modules, written out to `describe.json`.
- Whether a service gets placeholder reasoning before a tool call is set per service: DeepSeek gets it, OpenRouter does not.
- ChatGPT plan: a final response with an empty `output` takes the items of its `output_item.done` events.
