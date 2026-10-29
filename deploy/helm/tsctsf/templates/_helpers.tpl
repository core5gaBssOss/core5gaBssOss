{{/*
The NF's config: the generated files/tsctsf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "tsctsf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/tsctsf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
