{{/*
The NF's config: the generated files/scp.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "scp.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/scp.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
