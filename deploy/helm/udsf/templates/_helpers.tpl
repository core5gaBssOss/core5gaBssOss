{{/*
The NF's config: the generated files/udsf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "udsf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/udsf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
