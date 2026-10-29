{{/*
The NF's config: the generated files/udr.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "udr.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/udr.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
