package main

import (
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestTranscribeAudio(t *testing.T) {
	t.Run("rejects missing key", func(t *testing.T) {
		app := &App{client: http.DefaultClient}
		_, err := app.TranscribeAudio(base64.StdEncoding.EncodeToString([]byte("audio")), "audio/webm")
		if err == nil {
			t.Fatal("expected missing key error")
		}
	})

	t.Run("rejects unsupported mime type", func(t *testing.T) {
		app := &App{client: http.DefaultClient, apiKey: "test-key"}
		_, err := app.TranscribeAudio(base64.StdEncoding.EncodeToString([]byte("audio")), "application/octet-stream")
		if err == nil {
			t.Fatal("expected unsupported mime error")
		}
	})

	t.Run("forwards audio and returns transcript", func(t *testing.T) {
		server := httptest.NewServer(http.HandlerFunc(func(response http.ResponseWriter, request *http.Request) {
			if request.Header.Get("Authorization") != "Token test-key" {
				t.Errorf("unexpected authorization header: %q", request.Header.Get("Authorization"))
			}
			if request.Header.Get("Content-Type") != "audio/webm" {
				t.Errorf("unexpected content type: %q", request.Header.Get("Content-Type"))
			}
			if request.URL.Query().Get("model") != "nova-3" || request.URL.Query().Get("language") != "pt-BR" {
				t.Errorf("unexpected query: %s", request.URL.RawQuery)
			}
			body, _ := io.ReadAll(request.Body)
			if string(body) != "recorded-audio" {
				t.Errorf("unexpected audio body: %q", body)
			}
			_ = json.NewEncoder(response).Encode(map[string]any{
				"results": map[string]any{
					"channels": []any{map[string]any{
						"alternatives": []any{map[string]any{"transcript": "Texto transcrito."}},
					}},
				},
			})
		}))
		defer server.Close()

		app := &App{
			client:      &http.Client{Timeout: time.Second},
			apiKey:      "test-key",
			language:    "pt-BR",
			model:       "nova-3",
			deepgramURL: server.URL,
		}
		encoded := base64.StdEncoding.EncodeToString([]byte("recorded-audio"))
		transcript, err := app.TranscribeAudio(encoded, "audio/webm")
		if err != nil {
			t.Fatal(err)
		}
		if transcript != "Texto transcrito." {
			t.Fatalf("unexpected transcript: %q", transcript)
		}
	})
}
