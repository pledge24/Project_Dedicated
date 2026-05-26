// 입력 형식 검증. 위반 시 AppError(VALIDATION_FAILED) throw.
const { AppError, Codes } = require('./errors');

const RX_LOGIN_ID = /^[A-Za-z0-9_]{4,20}$/;
const RX_PASSWORD_LEN = /^.{8,64}$/;
const RX_PASSWORD_HAS_LETTER = /[A-Za-z]/;
const RX_PASSWORD_HAS_DIGIT = /[0-9]/;
// 한글(가-힣) + 영문 + 숫자 + 단일 공백 없음. 2~12자.
const RX_NICKNAME = /^[가-힣A-Za-z0-9]{2,12}$/;

/**
 * @param {*} v
 * @returns {string}
 */
function asString(v)
{
    return typeof v === 'string' ? v : '';
}

function validateLoginId(loginId)
{
    if (!RX_LOGIN_ID.test(asString(loginId)))
    {
        throw new AppError(Codes.VALIDATION_FAILED, 'ID는 영문/숫자/_ 조합으로 4~20자여야 합니다.');
    }
}

function validatePassword(password)
{
    const s = asString(password);
    if (!RX_PASSWORD_LEN.test(s))
    {
        throw new AppError(Codes.VALIDATION_FAILED, '비밀번호는 8~64자여야 합니다.');
    }
    if (!RX_PASSWORD_HAS_LETTER.test(s) || !RX_PASSWORD_HAS_DIGIT.test(s))
    {
        throw new AppError(Codes.VALIDATION_FAILED, '비밀번호는 영문자와 숫자를 각각 1자 이상 포함해야 합니다.');
    }
}

function validateNickname(nickname)
{
    const s = asString(nickname);
    if (s !== s.trim())
    {
        throw new AppError(Codes.VALIDATION_FAILED, '닉네임 앞뒤 공백은 허용되지 않습니다.');
    }
    if (!RX_NICKNAME.test(s))
    {
        throw new AppError(Codes.VALIDATION_FAILED, '닉네임은 한글/영문/숫자 2~12자여야 합니다.');
    }
}

module.exports = { validateLoginId, validatePassword, validateNickname };
