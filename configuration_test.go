package webrtc

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestConfiguration_getICEServers(t *testing.T) {
	t.Run("Success", func(t *testing.T) {
		expectedServerStr := "stun:stun.l.google.com:19302"
		cfg := Configuration{
			ICEServers: []ICEServer{
				{
					URLs: []string{expectedServerStr},
				},
			},
		}

		parsedURLs, err := cfg.getICEServers()
		assert.Nil(t, err)
		assert.Equal(t, expectedServerStr, (*parsedURLs)[0].String())
	})

	t.Run("Failure", func(t *testing.T) {
		expectedServerStr := "stun.l.google.com:19302"
		cfg := Configuration{
			ICEServers: []ICEServer{
				{
					URLs: []string{expectedServerStr},
				},
			},
		}

		_, err := cfg.getICEServers()
		assert.NotNil(t, err)
	})

	t.Run("Success", func(t *testing.T) {
		// ignore the fact that stun URLs shouldn't have a query
		serverStr := "stun:global.stun.twilio.com:3478?transport=udp"
		expectedServerStr := "stun:global.stun.twilio.com:3478"
		cfg := Configuration{
			ICEServers: []ICEServer{
				{
					URLs: []string{serverStr},
				},
			},
		}

		parsedURLs, err := cfg.getICEServers()
		assert.Nil(t, err)
		assert.Equal(t, expectedServerStr, (*parsedURLs)[0].String())
	})
}
