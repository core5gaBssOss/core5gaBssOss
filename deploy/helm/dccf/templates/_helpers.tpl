{{/*
The NF's config: the generated files/dccf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "dccf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/dccf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
