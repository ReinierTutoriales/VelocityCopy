# Localization architecture

VelocityCopy treats localization as product infrastructure, not as a late translation pass.

## Application UI

The WinUI application uses Windows App SDK resource management (MRT Core) and `.resw` resources.

Initial structure:

```text
src/ui/Strings/
├── en-US/Resources.resw
└── es-ES/Resources.resw
```

`en-US` is the fallback/default resource set. `es-ES` is shipped from the first UI milestone.

Rules:

- Use stable semantic resource IDs. Do not encode English wording in resource IDs.
- Use `x:Uid` for XAML-localized controls where appropriate.
- Use `ResourceLoader` for strings created in code.
- Do not concatenate translated sentence fragments to build messages.
- Format numbers, dates, file sizes and durations using locale-aware APIs.
- UI containers must permit translation expansion and RTL flow.
- Accessibility names and tooltips are localized resources too.
- App/manifest display strings use `ms-resource` references once the package manifest is finalized.
- A new language should normally require a new BCP-47 resource folder, not changes to engine code.

## Explorer shell extension

`VelocityCopy.Shell.dll` must remain extremely small inside Explorer. It must not load WinUI merely to localize two menu labels.

Shell strings are therefore kept in native Windows resources/MUI-compatible resources. The shell DLL resolves only the small set of command labels/tooltips it owns and sends language-neutral action IDs over IPC.

This keeps Explorer integration independent from the application's UI framework and allows the application localization system to evolve without destabilizing Explorer.

## Language-neutral core

Core types expose enums, status codes and structured data. The core never returns a final user-facing sentence when a status/code can be returned instead.

Example:

```text
Core:      CopyError::DestinationFull + path + native_error
UI en-US:  "Not enough space on the destination drive."
UI es-ES:  "No hay suficiente espacio en la unidad de destino."
```

This rule applies to copy errors, queue state, conflict handling, resume state, shell actions and notifications.

## Future languages

Resource organization must support additional BCP-47 locales such as `fr-FR`, `de-DE`, `pt-BR`, `ar-SA` or others without altering copy behavior. RTL support is treated as a layout capability, not as a separate redesign.
