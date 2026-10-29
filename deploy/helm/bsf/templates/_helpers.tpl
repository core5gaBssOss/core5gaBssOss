{{/*
The NF's config: the generated files/bsf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "bsf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/bsf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
