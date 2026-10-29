{{/*
The NF's config: the generated files/smf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "smf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/smf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
