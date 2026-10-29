{{/*
The NF's config: the generated files/upf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "upf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/upf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
