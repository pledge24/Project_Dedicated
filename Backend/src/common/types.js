// JSDoc @typedef 모음. 런타임 코드 없음 (IDE 힌트용).

/**
 * @typedef {Object} BackendError
 * @property {string} code
 * @property {string} message
 */

/**
 * @typedef {Object} BackendResponse
 * @property {boolean} ok
 * @property {*} [data]
 * @property {BackendError} [error]
 */

/**
 * @typedef {Object} AuthUserDTO
 * @property {number} userId
 * @property {string} nickname
 * @property {number} score
 * @property {number} level
 * @property {number} exp
 * @property {string} token
 */

/**
 * @typedef {Object} RegisterResultDTO
 * @property {number} userId
 * @property {string} nickname
 * @property {number} score
 * @property {number} level
 * @property {number} exp
 */

/**
 * @typedef {Object} UserRow
 * @property {number} id
 * @property {string} login_id
 * @property {string} password_hash
 * @property {string} nickname
 */

/**
 * @typedef {Object} PlayerProfileRow
 * @property {number} user_id
 * @property {number} score
 * @property {number} level
 * @property {number} exp
 * @property {number} wins
 * @property {number} losses
 * @property {number} matches_played
 * @property {Date|null} last_match_at
 */

/**
 * @typedef {Object} MatchRowDTO
 * @property {number} id
 * @property {string} clientMatchId
 * @property {string} mapName
 * @property {string} startedAt
 * @property {string} endedAt
 * @property {number} durationSec
 * @property {'winner'|'draw'|'time_expired'|'abort'} endReason
 * @property {number|null} winnerUserId
 */

/**
 * @typedef {Object} MatchParticipantRowDTO
 * @property {number} matchId
 * @property {number} userId
 * @property {string} nicknameSnapshot
 * @property {number} slotIndex
 * @property {number} placement
 * @property {number} livesLeft
 * @property {number} expGained
 * @property {number} scoreDelta
 */

export {};
