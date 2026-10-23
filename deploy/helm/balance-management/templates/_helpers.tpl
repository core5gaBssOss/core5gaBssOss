{{/*
The component's config: the generated files/balance-management.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "balance-management.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/balance-management.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
