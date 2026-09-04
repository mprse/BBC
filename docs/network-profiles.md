# BBC Network Profiles

- Status: Accepted
- Scope: Stage 7.3

BBC has two isolated network profiles. A profile fixes the chain ID, P2P magic,
Genesis Block ID, and Proof-of-Work target. Data directories and blocks cannot
be reused across profiles.

| Profile | Chain ID | P2P magic | Non-Genesis target | Expected attempts |
| --- | ---: | --- | --- | ---: |
| `development` | 1 | `42 42 43 01` | `000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF` | 16,777,216 |
| `regtest` | 2 | `42 42 43 02` | `000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF` | 4,096 |

The target comparison treats the 32-byte hash and target as unsigned big-endian
integers and requires `hash < target`. Genesis uses the maximum target, a zero
mining nonce, and is never mined. Its canonical block encoding contains the
profile chain ID, so the two profiles have distinct Genesis Block IDs.

`development` is intended for a visible human demonstration. At the current
single-threaded debug hash rate it normally takes tens of seconds. `regtest` is
only for automated and rapid local tests. It exercises real SHA-256 mining but
does not model meaningful economic work or security.

Every generated actor configuration contains a required `network` string. A
node opens or creates its chain store for that profile and rejects P2P frames,
HELLO identities, transactions, blocks, and stored Genesis data from the other
profile.
