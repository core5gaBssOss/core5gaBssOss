{{/*
The NF's config: the generated files/chf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "chf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/chf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
