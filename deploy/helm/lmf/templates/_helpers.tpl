{{/*
The NF's config: the generated files/lmf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "lmf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/lmf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
