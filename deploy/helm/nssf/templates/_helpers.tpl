{{/*
The NF's config: the generated files/nssf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "nssf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/nssf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
