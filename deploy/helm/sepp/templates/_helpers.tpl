{{/*
The NF's config: the generated files/sepp.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "sepp.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/sepp.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
