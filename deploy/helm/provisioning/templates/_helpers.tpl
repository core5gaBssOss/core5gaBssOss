{{/*
The component's config: the generated files/provisioning.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "provisioning.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/provisioning.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
