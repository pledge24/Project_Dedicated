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
 * @property {string} token
 */

/**
 * @typedef {Object} UserRow
 * @property {number} id
 * @property {string} login_id
 * @property {string} password_hash
 * @property {string} nickname
 * @property {number} score
 */

module.exports = {};
