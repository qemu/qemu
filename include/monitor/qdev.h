#ifndef MONITOR_QDEV_H
#define MONITOR_QDEV_H

/*** monitor commands ***/

void hmp_info_qtree(Monitor *mon, const QDict *qdict);
void hmp_info_qdm(Monitor *mon, const QDict *qdict);
void qmp_device_add(QDict *qdict, QObject **ret_data, Error **errp);

int qdev_device_help(QemuOpts *opts);
DeviceState *qdev_device_add(QemuOpts *opts, Error **errp);
DeviceState *qdev_device_add_from_qdict(const QDict *opts,
                                        bool from_json, Error **errp);

/**
 * qdev_set_id: parent the device and set its id if provided.
 * @dev: device to handle
 * @id: id to be given to the device, or NULL.
 *
 * Returns: the id of the device in case of success; otherwise NULL.
 *
 * @dev must be unrealized, unparented and must not have an id.
 *
 * If @id is non-NULL, this function tries to setup @dev qom path as
 * "/peripheral/id". If @id is already taken, it fails. If it succeeds,
 * the id field of @dev is set to @id (@dev now owns the given @id
 * parameter).
 *
 * If @id is NULL, this function generates a unique name and setups @dev
 * qom path as "/peripheral-anon/name". This name is not set as the id
 * of @dev.
 *
 * Upon success, it returns the id/name (generated or provided). The
 * returned string is owned by the corresponding child property and must
 * not be freed by the caller.
 */
const char *qdev_set_id(DeviceState *dev, char *id, Error **errp);

#endif
