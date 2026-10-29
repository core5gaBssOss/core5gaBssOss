{{/*
The NF's config: the generated files/nsacf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "nsacf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/nsacf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
