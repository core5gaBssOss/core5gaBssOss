{{/*
The NF's config: the generated files/ausf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "ausf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/ausf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
