package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"sort"
	"strings"
	"syscall"
	"time"

	"github.com/cenkalti/rain/v2/torrent"
)

func folder(key, fallback string) string {
	if path := os.Getenv(key); path != "" {
		return path
	}
	return fallback
}
func atomicFile(path string, data []byte) error {
	file, err := os.CreateTemp(filepath.Dir(path), ".kefiriz-")
	if err != nil {
		return err
	}
	defer os.Remove(file.Name())
	if _, err = file.Write(data); err != nil {
		file.Close()
		return err
	}
	if err = file.Close(); err != nil {
		return err
	}
	return os.Rename(file.Name(), path)
}
func runTransfers(ctx context.Context) error {
	root := folder("KEFIRIZ_STATE_DIR", "/storage/kefiriz")
	for _, name := range []string{"queue", "downloads", "inbox", "completed"} {
		if err := os.MkdirAll(filepath.Join(root, name), 0700); err != nil {
			return err
		}
	}
	cfg := torrent.DefaultConfig
	cfg.Database = filepath.Join(root, "transfers.db")
	cfg.DataDir = filepath.Join(root, "downloads")
	cfg.DataDirIncludesTorrentID = true
	cfg.RPCEnabled = false
	cfg.ResumeOnStartup = true
	cfg.BlocklistURL = ""
	cfg.TrackerHTTPVerifyTLS = true
	cfg.ReadCacheSize = 8 << 20
	cfg.WriteCacheSize = 16 << 20
	cfg.MaxPeerDial = 12
	cfg.MaxPeerAccept = 8
	cfg.WebseedMaxDownloads = 2
	cfg.DHTEnabled = true
	session, err := torrent.NewSession(cfg)
	if err != nil {
		return err
	}
	defer session.Close()
	snapshot := func() {
		jobs, _ := os.ReadDir(filepath.Join(root, "queue"))
		for _, job := range jobs {
			if !job.Type().IsRegular() || !strings.HasSuffix(job.Name(), ".torrent") {
				continue
			}
			path := filepath.Join(root, "queue", job.Name())
			f, err := os.Open(path)
			if err != nil {
				continue
			}
			h := sha256.New()
			n, err := io.Copy(h, io.LimitReader(f, 4*1024*1024+1))
			if err == nil && n > 4*1024*1024 {
				err = fmt.Errorf("metadata is too large")
			}
			if err == nil {
				id := hex.EncodeToString(h.Sum(nil))
				if session.GetTorrent(id) == nil {
					_, err = f.Seek(0, 0)
					if err == nil {
						_, err = session.AddTorrent(f, &torrent.AddTorrentOptions{ID: id})
					}
				}
			}
			f.Close()
			if err != nil {
				log.Printf("queue %q: %v", job.Name(), err)
				if os.Rename(path, path+".invalid") == nil {
					atomicFile(filepath.Join(root, "transfer-error"), []byte("Could not read "+job.Name()))
				}
			} else {
				os.Remove(path)
			}
		}
		var rows []string
		for _, t := range session.ListTorrents() {
			s := t.Stats()
			value := s.Status.String()
			mark := filepath.Join(root, "completed", t.ID())
			if s.Bytes.Total > 0 {
				value = fmt.Sprintf("%d%%  %s", 100*s.Bytes.Completed/s.Bytes.Total, value)
				if s.Bytes.Completed == s.Bytes.Total {
					value = "Complete"
					if _, err := os.Stat(mark); os.IsNotExist(err) {
						if err := atomicFile(mark, []byte("verified\n")); err != nil {
							log.Printf("saving completion: %v", err)
						}
					}
					// Rain releases its live piece counters on stop. Save the verified
					// completion first so the UI survives both a stop and a reboot.
					if _, err := os.Stat(mark); err == nil && s.Status == torrent.Seeding {
						t.Stop()
					}
				}
			}
			if s.Status == torrent.Stopped {
				if _, err := os.Stat(mark); err == nil {
					value = "Complete"
				}
				if s.Error != nil {
					value = "Error: " + s.Error.Error()
				}
			}
			name := strings.Map(func(r rune) rune {
				if r < 32 || r == 127 {
					return ' '
				}
				return r
			}, t.Name())
			rows = append(rows, name+"\t"+value+"\n")
		}
		sort.Strings(rows)
		// Avoid rewriting the microSD when displayed progress has not changed.
		dest := filepath.Join(root, "transfers.tsv")
		data := strings.Join(rows, "")
		old, _ := os.ReadFile(dest)
		if string(old) != data {
			atomicFile(dest, []byte(data))
		}
	}
	snapshot()
	ticker := time.NewTicker(3 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return nil
		case <-ticker.C:
			snapshot()
		}
	}
}
func main() {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	var err error
	if len(os.Args) == 2 && os.Args[1] == "network" {
		err = runNetwork(ctx)
	} else {
		err = runTransfers(ctx)
	}
	if err != nil {
		log.Fatal(err)
	}
}
