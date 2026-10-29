{{/*
The NF's config: the generated files/nrf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "nrf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/nrf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
