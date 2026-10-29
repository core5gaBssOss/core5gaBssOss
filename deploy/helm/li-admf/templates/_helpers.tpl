{{/*
The NF's config: the generated files/li-admf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "li-admf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/li-admf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
