package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

type App struct {
	ctx         context.Context
	client      *http.Client
	apiKey      string
	language    string
	model       string
	deepgramURL string
}

func NewApp() *App {
	loadDotEnv()
	return &App{
		client:      &http.Client{Timeout: 90 * time.Second},
		apiKey:      os.Getenv("DEEPGRAM_API_KEY"),
		language:    envOrDefault("DEEPGRAM_LANGUAGE", "pt-BR"),
		model:       envOrDefault("DEEPGRAM_MODEL", "nova-3"),
		deepgramURL: "https://api.deepgram.com/v1/listen",
	}
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
}

func (a *App) TranscribeAudio(encodedAudio, mimeType string) (string, error) {
	if a.apiKey == "" {
		return "", errors.New("DEEPGRAM_API_KEY não configurada; coloque um .env ao lado do executável")
	}
	mimeType = strings.ToLower(strings.TrimSpace(mimeType))
	supportedMime := map[string]bool{
		"audio/webm":             true,
		"audio/webm;codecs=opus": true,
		"audio/ogg":              true,
		"audio/ogg;codecs=opus":  true,
		"audio/mp4":              true,
		"audio/wav":              true,
	}
	if !supportedMime[mimeType] {
		return "", errors.New("formato de áudio inválido")
	}
	maxEncoded := ((25*1024*1024 + 2) / 3) * 4
	if len(encodedAudio) > maxEncoded {
		return "", errors.New("o áudio excede o limite de 25 MB")
	}
	audio, err := base64.StdEncoding.DecodeString(encodedAudio)
	if err != nil {
		return "", fmt.Errorf("áudio inválido: %w", err)
	}
	if len(audio) == 0 {
		return "", errors.New("nenhum áudio foi capturado")
	}
	if len(audio) > 25*1024*1024 {
		return "", errors.New("o áudio excede o limite de 25 MB")
	}

	endpoint, err := url.Parse(a.deepgramURL)
	if err != nil {
		return "", fmt.Errorf("endpoint Deepgram inválido: %w", err)
	}
	query := endpoint.Query()
	query.Set("dictation", "true")
	query.Set("language", a.language)
	query.Set("model", a.model)
	query.Set("punctuate", "true")
	query.Set("smart_format", "true")
	endpoint.RawQuery = query.Encode()

	ctx := a.ctx
	if ctx == nil {
		ctx = context.Background()
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint.String(), bytes.NewReader(audio))
	if err != nil {
		return "", err
	}
	req.Header.Set("Authorization", "Token "+a.apiKey)
	req.Header.Set("Content-Type", mimeType)

	response, err := a.client.Do(req)
	if err != nil {
		return "", fmt.Errorf("falha ao acessar o Deepgram: %w", err)
	}
	defer response.Body.Close()
	body, err := io.ReadAll(io.LimitReader(response.Body, 2*1024*1024))
	if err != nil {
		return "", err
	}

	var payload struct {
		Message string `json:"message"`
		ErrMsg  string `json:"err_msg"`
		Results struct {
			Channels []struct {
				Alternatives []struct {
					Transcript string `json:"transcript"`
				} `json:"alternatives"`
			} `json:"channels"`
		} `json:"results"`
	}
	if err := json.Unmarshal(body, &payload); err != nil {
		return "", fmt.Errorf("resposta inválida do Deepgram: %w", err)
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		message := payload.ErrMsg
		if message == "" {
			message = payload.Message
		}
		if message == "" {
			message = response.Status
		}
		return "", errors.New(message)
	}
	if len(payload.Results.Channels) == 0 || len(payload.Results.Channels[0].Alternatives) == 0 {
		return "", errors.New("nenhuma fala foi reconhecida")
	}
	transcript := strings.TrimSpace(payload.Results.Channels[0].Alternatives[0].Transcript)
	if transcript == "" {
		return "", errors.New("nenhuma fala foi reconhecida")
	}
	return transcript, nil
}

func envOrDefault(name, fallback string) string {
	if value := strings.TrimSpace(os.Getenv(name)); value != "" {
		return value
	}
	return fallback
}

func loadDotEnv() {
	candidates := make([]string, 0, 5)
	if executable, err := os.Executable(); err == nil {
		directory := filepath.Dir(executable)
		for range 5 {
			candidates = append(candidates, filepath.Join(directory, ".env"))
			directory = filepath.Dir(directory)
		}
	}
	for _, candidate := range candidates {
		contents, err := os.ReadFile(candidate)
		if err != nil {
			continue
		}
		for _, line := range strings.Split(string(contents), "\n") {
			line = strings.TrimSpace(line)
			if line == "" || strings.HasPrefix(line, "#") {
				continue
			}
			name, value, ok := strings.Cut(line, "=")
			name = strings.TrimSpace(name)
			if !ok || (name != "DEEPGRAM_API_KEY" && name != "DEEPGRAM_LANGUAGE" && name != "DEEPGRAM_MODEL") || os.Getenv(name) != "" {
				continue
			}
			value = strings.Trim(strings.TrimSpace(value), `"'`)
			_ = os.Setenv(name, value)
		}
		return
	}
}
