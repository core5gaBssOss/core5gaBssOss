{{/*
The NF's config: the generated files/adrf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "adrf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/adrf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
