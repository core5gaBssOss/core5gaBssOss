{{/*
The NF's config: the generated files/gmlc.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "gmlc.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/gmlc.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
