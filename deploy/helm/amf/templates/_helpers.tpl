{{/*
The NF's config: the generated files/amf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "amf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/amf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
