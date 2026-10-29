{{/*
The NF's config: the generated files/nef.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "nef.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/nef.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
