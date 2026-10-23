{{/*
The NF's config: the generated files/udm.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "udm.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/udm.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
