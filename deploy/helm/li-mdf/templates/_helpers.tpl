{{/*
The NF's config: the generated files/li-mdf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "li-mdf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/li-mdf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
