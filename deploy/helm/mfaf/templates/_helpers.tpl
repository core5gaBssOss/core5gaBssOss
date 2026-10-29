{{/*
The NF's config: the generated files/mfaf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "mfaf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/mfaf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
